/*
 * Copyright 2015 - 2016 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mpp_enc_jpeg_gen_hdr.h"

#if 0
typedef struct JpegeSyntax_t {
    uint32_t          width;
    uint32_t          height;
    uint32_t		format;

    /* For quantization table */
    uint32_t          quality;
    uint8_t           *qtable_y;
    uint8_t           *qtable_c;

    /*
     * For color conversion
     *
     * 0 = bt601
     * 1 = bt709
     * 2 = user defined
     */
    uint32_t          color_conversion_type;
    uint32_t          coeffA;
    uint32_t          coeffB;
    uint32_t          coeffC;
    uint32_t          coeffE;
    uint32_t          coeffF;

    /* For slice encoding mode */
    uint32_t          slice_enable;
    uint32_t          slice_size_mb_rows;
    uint32_t          restart_interval;

    /*
     * For unit type and density
     *
     * units_type   0 - no unit
     *              1 - dots per inch
     *              2 - dots per cm
     *
     * X/Y density  specify the pixel aspect ratio
     */
    uint32_t          units_type;
    uint32_t          density_x;
    uint32_t          density_y;

    /* For comment header */
    uint32_t          comment_length;
    uint8_t           *comment_data;
} JpegeSyntax;
#endif

#define MAX_NUMBER_OF_COMPONENTS 3

/* JPEG markers, table B.1 */
enum {
	SOI = 0xFFD8,		/* Start of Image                    */
	DQT = 0xFFDB,		/* Define Quantization Table(s)      */
	SOF0 = 0xFFC0,		/* Start of Frame                    */
	DRI = 0xFFDD,		/* Define Restart Interval           */
	RST0 = 0xFFD0,		/* Restart marker 0                  */
	RST1 = 0xFFD1,		/* Restart marker 1                  */
	RST2 = 0xFFD2,		/* Restart marker 2                  */
	RST3 = 0xFFD3,		/* Restart marker 3                  */
	RST4 = 0xFFD4,		/* Restart marker 4                  */
	RST5 = 0xFFD5,		/* Restart marker 5                  */
	RST6 = 0xFFD6,		/* Restart marker 6                  */
	RST7 = 0xFFD7,		/* Restart marker 7                  */
	DHT = 0xFFC4,		/* Define Huffman Table(s)           */
	SOS = 0xFFDA,		/* Start of Scan                     */
	EOI = 0xFFD9,		/* End of Image                      */
	APP0 = 0xFFE0,		/* APP0 Marker                       */
	COM = 0xFFFE		/* Comment marker                    */
};

enum {
	JPEGE_NO_UNITS = 0,
	JPEGE_DOTS_PER_INCH = 1,
	JPEGE_DOTS_PER_CM = 2
};

enum {
	JPEGE_SINGLE_MARKER,
	JPEGE_MULTI_MARKER
};

static const uint8_t zigzag[64] = {
	0, 1, 8, 16, 9, 2, 3, 10,
	17, 24, 32, 25, 18, 11, 4, 5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13, 6, 7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

typedef struct {
	uint32_t Ci[MAX_NUMBER_OF_COMPONENTS];
	uint32_t Hi[MAX_NUMBER_OF_COMPONENTS];
	uint32_t Vi[MAX_NUMBER_OF_COMPONENTS];
	uint32_t Tqi[MAX_NUMBER_OF_COMPONENTS];
} JpegeColorInfo;

static const JpegeColorInfo color_info[2] = {
	{
	 /* YUV420 */
	 {1, 2, 3,},
	 {2, 1, 1,},
	 {2, 1, 1,},
	 {0, 1, 1,},
	 },
	{
	 /* YUV422 */
	 {1, 2, 3,},
	 {2, 1, 1,},
	 {1, 1, 1,},
	 {0, 1, 1,},
	 },
};

typedef struct {
	uint32_t val_y;
	uint32_t val_c;
} JpegeHuffmanTable;

static const JpegeHuffmanTable dc_li[16] = {
	{0x00, 0x00},
	{0x01, 0x03},
	{0x05, 0x01},
	{0x01, 0x01},
	{0x01, 0x01},
	{0x01, 0x01},
	{0x01, 0x01},
	{0x01, 0x01},
	{0x01, 0x01},
	{0x00, 0x01},
	{0x00, 0x01},
	{0x00, 0x00},
	{0x00, 0x00},
	{0x00, 0x00},
	{0x00, 0x00},
	{0x00, 0x00}
};

static const JpegeHuffmanTable dc_vij[12] = {
	{0x00, 0x00},
	{0x01, 0x01},
	{0x02, 0x02},
	{0x03, 0x03},
	{0x04, 0x04},
	{0x05, 0x05},
	{0x06, 0x06},
	{0x07, 0x07},
	{0x08, 0x08},
	{0x09, 0x09},
	{0x0A, 0x0A},
	{0x0B, 0x0B},
};

static const JpegeHuffmanTable ac_li[16] = {
	{0x00, 0x00},
	{0x02, 0x02},
	{0x01, 0x01},
	{0x03, 0x02},
	{0x03, 0x04},
	{0x02, 0x04},
	{0x04, 0x03},
	{0x03, 0x04},
	{0x05, 0x07},
	{0x05, 0x05},
	{0x04, 0x04},
	{0x04, 0x04},
	{0x00, 0x00},
	{0x00, 0x01},
	{0x01, 0x02},
	{0x7D, 0x77}
};

static const JpegeHuffmanTable ac_vij[162] = {
	{0x01, 0x00},
	{0x02, 0x01},
	{0x03, 0x02},
	{0x00, 0x03},
	{0x04, 0x11},
	{0x11, 0x04},
	{0x05, 0x05},
	{0x12, 0x21},
	{0x21, 0x31},
	{0x31, 0x06},
	{0x41, 0x12},
	{0x06, 0x41},
	{0x13, 0x51},
	{0x51, 0x07},
	{0x61, 0x61},
	{0x07, 0x71},
	{0x22, 0x13},
	{0x71, 0x22},
	{0x14, 0x32},
	{0x32, 0x81},
	{0x81, 0x08},
	{0x91, 0x14},
	{0xA1, 0x42},
	{0x08, 0x91},
	{0x23, 0xA1},
	{0x42, 0xB1},
	{0xB1, 0xC1},
	{0xC1, 0x09},
	{0x15, 0x23},
	{0x52, 0x33},
	{0xD1, 0x52},
	{0xF0, 0xF0},
	{0x24, 0x15},
	{0x33, 0x62},
	{0x62, 0x72},
	{0x72, 0xD1},
	{0x82, 0x0A},
	{0x09, 0x16},
	{0x0A, 0x24},
	{0x16, 0x34},
	{0x17, 0xE1},
	{0x18, 0x25},
	{0x19, 0xF1},
	{0x1A, 0x17},
	{0x25, 0x18},
	{0x26, 0x19},
	{0x27, 0x1A},
	{0x28, 0x26},
	{0x29, 0x27},
	{0x2A, 0x28},
	{0x34, 0x29},
	{0x35, 0x2A},
	{0x36, 0x35},
	{0x37, 0x36},
	{0x38, 0x37},
	{0x39, 0x38},
	{0x3A, 0x39},
	{0x43, 0x3A},
	{0x44, 0x43},
	{0x45, 0x44},
	{0x46, 0x45},
	{0x47, 0x46},
	{0x48, 0x47},
	{0x49, 0x48},
	{0x4A, 0x49},
	{0x53, 0x4A},
	{0x54, 0x53},
	{0x55, 0x54},
	{0x56, 0x55},
	{0x57, 0x56},
	{0x58, 0x57},
	{0x59, 0x58},
	{0x5A, 0x59},
	{0x63, 0x5A},
	{0x64, 0x63},
	{0x65, 0x64},
	{0x66, 0x65},
	{0x67, 0x66},
	{0x68, 0x67},
	{0x69, 0x68},
	{0x6A, 0x69},
	{0x73, 0x6A},
	{0x74, 0x73},
	{0x75, 0x74},
	{0x76, 0x75},
	{0x77, 0x76},
	{0x78, 0x77},
	{0x79, 0x78},
	{0x7A, 0x79},
	{0x83, 0x7A},
	{0x84, 0x82},
	{0x85, 0x83},
	{0x86, 0x84},
	{0x87, 0x85},
	{0x88, 0x86},
	{0x89, 0x87},
	{0x8A, 0x88},
	{0x92, 0x89},
	{0x93, 0x8A},
	{0x94, 0x92},
	{0x95, 0x93},
	{0x96, 0x94},
	{0x97, 0x95},
	{0x98, 0x96},
	{0x99, 0x97},
	{0x9A, 0x98},
	{0xA2, 0x99},
	{0xA3, 0x9A},
	{0xA4, 0xA2},
	{0xA5, 0xA3},
	{0xA6, 0xA4},
	{0xA7, 0xA5},
	{0xA8, 0xA6},
	{0xA9, 0xA7},
	{0xAA, 0xA8},
	{0xB2, 0xA9},
	{0xB3, 0xAA},
	{0xB4, 0xB2},
	{0xB5, 0xB3},
	{0xB6, 0xB4},
	{0xB7, 0xB5},
	{0xB8, 0xB6},
	{0xB9, 0xB7},
	{0xBA, 0xB8},
	{0xC2, 0xB9},
	{0xC3, 0xBA},
	{0xC4, 0xC2},
	{0xC5, 0xC3},
	{0xC6, 0xC4},
	{0xC7, 0xC5},
	{0xC8, 0xC6},
	{0xC9, 0xC7},
	{0xCA, 0xC8},
	{0xD2, 0xC9},
	{0xD3, 0xCA},
	{0xD4, 0xD2},
	{0xD5, 0xD3},
	{0xD6, 0xD4},
	{0xD7, 0xD5},
	{0xD8, 0xD6},
	{0xD9, 0xD7},
	{0xDA, 0xD8},
	{0xE1, 0xD9},
	{0xE2, 0xDA},
	{0xE3, 0xE2},
	{0xE4, 0xE3},
	{0xE5, 0xE4},
	{0xE6, 0xE5},
	{0xE7, 0xE6},
	{0xE8, 0xE7},
	{0xE9, 0xE8},
	{0xEA, 0xE9},
	{0xF1, 0xEA},
	{0xF2, 0xF2},
	{0xF3, 0xF3},
	{0xF4, 0xF4},
	{0xF5, 0xF5},
	{0xF6, 0xF6},
	{0xF7, 0xF7},
	{0xF8, 0xF8},
	{0xF9, 0xF9},
	{0xFA, 0xFA}
};

typedef struct {
	uint8_t *buffer;		/* Pointer to first byte of stream */
	uint8_t *stream;		/* Pointer to next byte of stream */
	uint32_t size;		/* Byte size of stream buffer */
	uint32_t byteCnt;		/* Byte counter */
	uint32_t bitCnt;		/* Bit counter */
	uint32_t byteBuffer;	/* Byte buffer */
	uint32_t bufferedBits;	/* Amount of bits in byte buffer, [0-7] */
} JpegeBitsImpl;

void jpege_bits_init(void **ctx)
{
	JpegeBitsImpl *impl = calloc(sizeof(JpegeBitsImpl), 1);
	*ctx = impl;
}

void jpege_bits_deinit(void *ctx)
{
	if (ctx)
		free(ctx);
}

void jpege_bits_setup(void *ctx, uint8_t * buf, int32_t size)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;

	impl->buffer = buf;
	impl->stream = buf;
	impl->size = size;
	impl->byteCnt = 0;
	impl->bitCnt = 0;
	impl->byteBuffer = 0;
	impl->bufferedBits = 0;
	buf[0] = 0;
	buf[1] = 0;
}

static void jpege_bits_put(void *ctx, uint32_t value, int32_t number)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;

	uint32_t bits;
	uint32_t byteBuffer = impl->byteBuffer;
	uint8_t *stream = impl->stream;

	/* Debug: value is too big */
	assert(value < ((uint32_t) 1 << number));
	assert(number < 25);

	bits = number + impl->bufferedBits;

	value <<= (32 - bits);
	byteBuffer = (((uint32_t) stream[0]) << 24) | value;

	while (bits > 7) {
		*stream = (uint8_t) (byteBuffer >> 24);
		bits -= 8;
		byteBuffer <<= 8;
		stream++;
		impl->byteCnt++;
	}

	stream[0] = (uint8_t) (byteBuffer >> 24);
	impl->stream = stream;
	impl->bitCnt += number;
	impl->byteBuffer = byteBuffer;
	impl->bufferedBits = (uint8_t) bits;
}

void jpege_bits_align_byte(void *ctx)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;

	if (impl->bitCnt & 7)
		jpege_bits_put(ctx, 0, 8 - (impl->bitCnt & 7));
}

uint8_t *jpege_bits_get_buf(void *ctx)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;
	return impl->buffer;
}

int32_t jpege_bits_get_bitpos(void *ctx)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;
	return impl->bitCnt;
}

int32_t jpege_bits_get_bytepos(void *ctx)
{
	JpegeBitsImpl *impl = (JpegeBitsImpl *) ctx;
	return impl->byteCnt;
}

static void write_jpeg_app0_header(void *bits,
		struct object_surface *obj_surface)
{
	/* APP0 */
	jpege_bits_put(bits, APP0, 16);

	/* Length */
	jpege_bits_put(bits, 0x0010, 16);

	/* "JFIF" ID */
	/* Ident1 */
	jpege_bits_put(bits, 0x4A46, 16);
	/* Ident2 */
	jpege_bits_put(bits, 0x4946, 16);
	/* Ident3 */
	jpege_bits_put(bits, 0x00, 8);
	/* Version */
	jpege_bits_put(bits, 0x0102, 16);
#if 0
	if (syntax->density_x && syntax->density_y) {
		/* Units */
		jpege_bits_put(bits, syntax->units_type, 8);
		/* Xdensity */
		jpege_bits_put(bits, syntax->density_x, 16);
		/* Ydensity */
		jpege_bits_put(bits, syntax->density_y, 16);
	} else {
		/* Units */
		jpege_bits_put(bits, 0, 8);
		/* Xdensity */
		jpege_bits_put(bits, 1, 16);
		/* Ydensity */
		jpege_bits_put(bits, 1, 16);
	}
#else
	/* Units */
	jpege_bits_put(bits, 0, 8);
	/* Xdensity */
	jpege_bits_put(bits, 1, 16);
	/* Ydensity */
	jpege_bits_put(bits, 1, 16);
#endif

	/* XThumbnail */
	jpege_bits_put(bits, 0x00, 8);
	/* YThumbnail */
	jpege_bits_put(bits, 0x00, 8);

	/* Do NOT write thumbnail */
}

#if 0
static void write_jpeg_comment_header(void ** bits,
				      JpegeSyntax * syntax)
{
	uint32_t i;
	uint8_t *data = syntax->comment_data;
	uint32_t length = syntax->comment_length;

	/* COM */
	jpege_bits_put(bits, COM, 16);

	/* Lc */
	jpege_bits_put(bits, 2 + length, 16);

	for (i = 0; i < length; i++) {
		/* COM data */
		jpege_bits_put(bits, data[i], 8);
	}
}
#endif

static void write_jpeg_dqt_header(void *bits,
		  const uint8_t *qtables[2])
{
	int32_t i;
	const uint8_t *qtable = qtables[0];

	/* DQT */
	jpege_bits_put(bits, DQT, 16);
	/* Lq */
	jpege_bits_put(bits, 2 + 65, 16);
	/* Pq */
	jpege_bits_put(bits, 0, 4);
	/* Tq */
	jpege_bits_put(bits, 0, 4);

	for (i = 0; i < 64; i++) {
		/* Qk table 0 */
		jpege_bits_put(bits, qtable[zigzag[i]], 8);
	}

	/* DQT */
	jpege_bits_put(bits, DQT, 16);
	/* Lq */
	jpege_bits_put(bits, 2 + 65, 16);
	/* Pq */
	jpege_bits_put(bits, 0, 4);
	/* Tq */
	jpege_bits_put(bits, 1, 4);

	qtable = qtables[1];

	for (i = 0; i < 64; i++) {
		/* Qk table 1 */
		jpege_bits_put(bits, qtable[zigzag[i]], 8);
	}
}

static void write_jpeg_SOFO_header(void *bits,
	struct object_surface *obj_surface)
{
	int32_t i;
	uint32_t width = obj_surface->orig_width;
	uint32_t height = obj_surface->orig_height;
	const JpegeColorInfo *info = &color_info[0];

	/* SOF0 */
	jpege_bits_put(bits, SOF0, 16);

	/* Lf */
	jpege_bits_put(bits, (8 + 3 * 3), 16);
	/* P */
	jpege_bits_put(bits, 8, 8);
	/* Y */
	jpege_bits_put(bits, height, 16);
	/* X */
	jpege_bits_put(bits, width, 16);
	/* Nf */
	jpege_bits_put(bits, 3, 8);

	/* NOTE: only output 420 bits */
	for (i = 0; i < 3; i++) {
		/* Ci */
		jpege_bits_put(bits, info->Ci[i], 8);
		/* Hi */
		jpege_bits_put(bits, info->Hi[i], 4);
		/* Vi */
		jpege_bits_put(bits, info->Vi[i], 4);
		/* Tqi */
		jpege_bits_put(bits, info->Tqi[i], 8);
	}
}

static void write_jpeg_dht_header(void *bits)
{
	int32_t i;

	/* DHT  */
	jpege_bits_put(bits, DHT, 16);

	/* Huffman table for luminance DC components */
	/* Lh  */
	jpege_bits_put(bits, 2 + ((17 * 1) + ((1 * 12))), 16);
	/* TC */
	jpege_bits_put(bits, 0, 4);
	/* TH */
	jpege_bits_put(bits, 0, 4);

	for (i = 0; i < 16; i++) {
		/* Dc_Li */
		jpege_bits_put(bits, dc_li[i].val_y, 8);
	}

	for (i = 0; i < 12; i++) {
		/* Dc_Vij */
		jpege_bits_put(bits, dc_vij[i].val_y, 8);
	}

	/* DHT  */
	jpege_bits_put(bits, DHT, 16);

	/* Huffman table for luminance AC components */
	/* Lh */
	jpege_bits_put(bits, 2 + ((17 * 1) + ((1 * 162))), 16);
	/* TC */
	jpege_bits_put(bits, 1, 4);
	/* TH */
	jpege_bits_put(bits, 0, 4);

	for (i = 0; i < 16; i++) {
		/* Ac_Li */
		jpege_bits_put(bits, ac_li[i].val_y, 8);
	}

	for (i = 0; i < 162; i++) {
		/* Ac_Vij */
		jpege_bits_put(bits, ac_vij[i].val_y, 8);
	}

	/* Huffman table for chrominance DC components */
	/* DHT  */
	jpege_bits_put(bits, DHT, 16);
	/* Lh */
	jpege_bits_put(bits, 2 + ((17 * 1) + ((1 * 12))), 16);
	/* TC */
	jpege_bits_put(bits, 0, 4);
	/* TH */
	jpege_bits_put(bits, 1, 4);

	for (i = 0; i < 16; i++) {
		/* Dc_Li */
		jpege_bits_put(bits, dc_li[i].val_c, 8);
	}

	for (i = 0; i < 12; i++) {
		/* Dc_Vij */
		jpege_bits_put(bits, dc_vij[i].val_c, 8);
	}

	/* Huffman table for chrominance AC components */
	/* DHT  */
	jpege_bits_put(bits, DHT, 16);
	/* Lh */
	jpege_bits_put(bits, 2 + ((17 * 1) + ((1 * 162))), 16);
	/* TC */
	jpege_bits_put(bits, 1, 4);
	/* TH */
	jpege_bits_put(bits, 1, 4);

	for (i = 0; i < 16; i++) {
		/* Ac_Li */
		jpege_bits_put(bits, ac_li[i].val_c, 8);
	}

	for (i = 0; i < 162; i++) {
		/* Ac_Vij */
		jpege_bits_put(bits, ac_vij[i].val_c, 8);
	}
}

static void write_jpeg_sos_header(void *bits)
{
	uint32_t i;
	uint32_t Ns = 3;
	uint32_t Ls = (6 + (2 * Ns));

	/* SOS  */
	jpege_bits_put(bits, SOS, 16);
	/* Ls  */
	jpege_bits_put(bits, Ls, 16);
	/* Ns  */
	jpege_bits_put(bits, Ns, 8);

	for (i = 0; i < Ns; i++) {
		/* Csj */
		jpege_bits_put(bits, i + 1, 8);

		if (i == 0) {
			/* Tdj */
			jpege_bits_put(bits, 0, 4);
			/* Taj */
			jpege_bits_put(bits, 0, 4);
		} else {
			/* Tdj */
			jpege_bits_put(bits, 1, 4);
			/* Taj */
			jpege_bits_put(bits, 1, 4);
		}
	}

	/* Ss */
	jpege_bits_put(bits, 0, 8);
	/* Se */
	jpege_bits_put(bits, 63, 8);
	/* Ah */
	jpege_bits_put(bits, 0, 4);
	/* Al */
	jpege_bits_put(bits, 0, 4);
}

int32_t write_jpeg_header(void *bits, struct object_surface *obj_surface,
			  const uint8_t * qtables[2])
{
	/* SOI */
	jpege_bits_put(bits, SOI, 16);

	/* APP0 header */
	write_jpeg_app0_header(bits, obj_surface);

#if 0
	/* Com header */
	if (syntax->comment_length)
		write_jpeg_comment_header(bits, syntax);
#endif

	/* Quant header */
#if 0
	if (syntax->qtable_y)
		qtables[0] = syntax->qtable_y;
	else
		qtables[0] = qtable_y[syntax->quality];

	if (syntax->qtable_c)
		qtables[1] = syntax->qtable_c;
	else
		qtables[1] = qtable_c[syntax->quality];

	/* FIXME don't use fixup quality variable */
	qtables[0] = qtable_y[11];
	qtables[1] = qtable_c[11];
#endif

	write_jpeg_dqt_header(bits, qtables);

	/* Frame header */
	write_jpeg_SOFO_header(bits, obj_surface);

	/* Do NOT have Restart interval */

	/* Huffman header */
	write_jpeg_dht_header(bits);

	/* Scan header */
	write_jpeg_sos_header(bits);

	jpege_bits_align_byte(bits);

	return 0;
}
