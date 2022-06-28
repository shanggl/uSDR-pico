/* fix_fft.c - Fixed-point in-place DIT Fast Fourier Transform  */
/*
  All data are fixed-point uint16_t integers, in which -32768
  to +32768 represent -1.0 to +1.0 respectively. Integer
  arithmetic is used for speed, instead of the more natural
  floating-point.

  For the forward FFT (time -> freq), fixed scaling is
  performed to prevent arithmetic overflow, and to map a 0dB
  sine/cosine wave (i.e. amplitude = 32767) to two -6dB freq
  coefficients. The return value is always 0.

  For the inverse FFT (freq -> time), fixed scaling cannot be
  done, as two 0dB coefficients would sum to a peak amplitude
  of 64K, overflowing the 32k range of the fixed-point integers.
  Thus, the fix_fft() routine performs variable scaling, and
  returns a value which is the number of bits LEFT by which
  the output must be shifted to get the actual amplitude
  (i.e. if fix_fft() returns 3, each value of fr[] and fi[]
  must be multiplied by 8 (2**3) for proper scaling.
  Clearly, this cannot be done within fixed-point uint16_t
  integers. In practice, if the result is to be used as a
  filter, the scale_shift can usually be ignored, as the
  result will be approximately correctly normalized as is.

  Written by:  Tom Roberts  11/8/89
  Made portable:  Malcolm Slaney 12/15/94 malcolm@interval.com
  Enhanced:  Dimitrios P. Bouras  14 Jun 2006 dbouras@ieee.org
*/
/*
  This implementation uses a lookup table for bit reverse sorting,
  which adds 2kbyte to the memory footprint.
  The iFFT range detector has been optimized.
  The bitshifting of signed integers is undefined, so these have been
  replaced by divisions. The compiler will optimize it.
  The size is fixed at 1024.
*/

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "fix_fft.h"


/** Fixed point Sine lookup table, [-1, 1] == [-32766, 32767] **/
int16_t Sine[3*FFT_SIZE/4] =
{
	    0,    201,    402,    603,    804,   1005,   1206,   1406,
	 1607,   1808,   2009,   2209,   2410,   2610,   2811,   3011,
	 3211,   3411,   3611,   3811,   4011,   4210,   4409,   4608,
	 4807,   5006,   5205,   5403,   5601,   5799,   5997,   6195,
	 6392,   6589,   6786,   6982,   7179,   7375,   7571,   7766,
	 7961,   8156,   8351,   8545,   8739,   8932,   9126,   9319,
	 9511,   9703,   9895,  10087,  10278,  10469,  10659,  10849,
	11038,  11227,  11416,  11604,  11792,  11980,  12166,  12353,
	12539,  12724,  12909,  13094,  13278,  13462,  13645,  13827,
	14009,  14191,  14372,  14552,  14732,  14911,  15090,  15268,
	15446,  15623,  15799,  15975,  16150,  16325,  16499,  16672,
	16845,  17017,  17189,  17360,  17530,  17699,  17868,  18036,
	18204,  18371,  18537,  18702,  18867,  19031,  19194,  19357,
	19519,  19680,  19840,  20000,  20159,  20317,  20474,  20631,
	20787,  20942,  21096,  21249,  21402,  21554,  21705,  21855,
	22004,  22153,  22301,  22448,  22594,  22739,  22883,  23027,
	23169,  23311,  23452,  23592,  23731,  23869,  24006,  24143,
	24278,  24413,  24546,  24679,  24811,  24942,  25072,  25201,
	25329,  25456,  25582,  25707,  25831,  25954,  26077,  26198,
	26318,  26437,  26556,  26673,  26789,  26905,  27019,  27132,
	27244,  27355,  27466,  27575,  27683,  27790,  27896,  28001,
	28105,  28208,  28309,  28410,  28510,  28608,  28706,  28802,
	28897,  28992,  29085,  29177,  29268,  29358,  29446,  29534,
	29621,  29706,  29790,  29873,  29955,  30036,  30116,  30195,
	30272,  30349,  30424,  30498,  30571,  30643,  30713,  30783,
	30851,  30918,  30984,  31049,  31113,  31175,  31236,  31297,
	31356,  31413,  31470,  31525,  31580,  31633,  31684,  31735,
	31785,  31833,  31880,  31926,  31970,  32014,  32056,  32097,
	32137,  32176,  32213,  32249,  32284,  32318,  32350,  32382,
	32412,  32441,  32468,  32495,  32520,  32544,  32567,  32588,
	32609,  32628,  32646,  32662,  32678,  32692,  32705,  32717,
	32727,  32736,  32744,  32751,  32757,  32761,  32764,  32766,
	32767,  32766,  32764,  32761,  32757,  32751,  32744,  32736,
	32727,  32717,  32705,  32692,  32678,  32662,  32646,  32628,
	32609,  32588,  32567,  32544,  32520,  32495,  32468,  32441,
	32412,  32382,  32350,  32318,  32284,  32249,  32213,  32176,
	32137,  32097,  32056,  32014,  31970,  31926,  31880,  31833,
	31785,  31735,  31684,  31633,  31580,  31525,  31470,  31413,
	31356,  31297,  31236,  31175,  31113,  31049,  30984,  30918,
	30851,  30783,  30713,  30643,  30571,  30498,  30424,  30349,
	30272,  30195,  30116,  30036,  29955,  29873,  29790,  29706,
	29621,  29534,  29446,  29358,  29268,  29177,  29085,  28992,
	28897,  28802,  28706,  28608,  28510,  28410,  28309,  28208,
	28105,  28001,  27896,  27790,  27683,  27575,  27466,  27355,
	27244,  27132,  27019,  26905,  26789,  26673,  26556,  26437,
	26318,  26198,  26077,  25954,  25831,  25707,  25582,  25456,
	25329,  25201,  25072,  24942,  24811,  24679,  24546,  24413,
	24278,  24143,  24006,  23869,  23731,  23592,  23452,  23311,
	23169,  23027,  22883,  22739,  22594,  22448,  22301,  22153,
	22004,  21855,  21705,  21554,  21402,  21249,  21096,  20942,
	20787,  20631,  20474,  20317,  20159,  20000,  19840,  19680,
	19519,  19357,  19194,  19031,  18867,  18702,  18537,  18371,
	18204,  18036,  17868,  17699,  17530,  17360,  17189,  17017,
	16845,  16672,  16499,  16325,  16150,  15975,  15799,  15623,
	15446,  15268,  15090,  14911,  14732,  14552,  14372,  14191,
	14009,  13827,  13645,  13462,  13278,  13094,  12909,  12724,
	12539,  12353,  12166,  11980,  11792,  11604,  11416,  11227,
	11038,  10849,  10659,  10469,  10278,  10087,   9895,   9703,
	 9511,   9319,   9126,   8932,   8739,   8545,   8351,   8156,
	 7961,   7766,   7571,   7375,   7179,   6982,   6786,   6589,
	 6392,   6195,   5997,   5799,   5601,   5403,   5205,   5006,
	 4807,   4608,   4409,   4210,   4011,   3811,   3611,   3411,
	 3211,   3011,   2811,   2610,   2410,   2209,   2009,   1808,
	 1607,   1406,   1206,   1005,    804,    603,    402,    201,
	    0,   -201,   -402,   -603,   -804,  -1005,  -1206,  -1406,
    -1607,  -1808,  -2009,  -2209,  -2410,  -2610,  -2811,  -3011,
    -3211,  -3411,  -3611,  -3811,  -4011,  -4210,  -4409,  -4608,
    -4807,  -5006,  -5205,  -5403,  -5601,  -5799,  -5997,  -6195,
    -6392,  -6589,  -6786,  -6982,  -7179,  -7375,  -7571,  -7766,
    -7961,  -8156,  -8351,  -8545,  -8739,  -8932,  -9126,  -9319,
    -9511,  -9703,  -9895, -10087, -10278, -10469, -10659, -10849,
   -11038, -11227, -11416, -11604, -11792, -11980, -12166, -12353,
   -12539, -12724, -12909, -13094, -13278, -13462, -13645, -13827,
   -14009, -14191, -14372, -14552, -14732, -14911, -15090, -15268,
   -15446, -15623, -15799, -15975, -16150, -16325, -16499, -16672,
   -16845, -17017, -17189, -17360, -17530, -17699, -17868, -18036,
   -18204, -18371, -18537, -18702, -18867, -19031, -19194, -19357,
   -19519, -19680, -19840, -20000, -20159, -20317, -20474, -20631,
   -20787, -20942, -21096, -21249, -21402, -21554, -21705, -21855,
   -22004, -22153, -22301, -22448, -22594, -22739, -22883, -23027,
   -23169, -23311, -23452, -23592, -23731, -23869, -24006, -24143,
   -24278, -24413, -24546, -24679, -24811, -24942, -25072, -25201,
   -25329, -25456, -25582, -25707, -25831, -25954, -26077, -26198,
   -26318, -26437, -26556, -26673, -26789, -26905, -27019, -27132,
   -27244, -27355, -27466, -27575, -27683, -27790, -27896, -28001,
   -28105, -28208, -28309, -28410, -28510, -28608, -28706, -28802,
   -28897, -28992, -29085, -29177, -29268, -29358, -29446, -29534,
   -29621, -29706, -29790, -29873, -29955, -30036, -30116, -30195,
   -30272, -30349, -30424, -30498, -30571, -30643, -30713, -30783,
   -30851, -30918, -30984, -31049, -31113, -31175, -31236, -31297,
   -31356, -31413, -31470, -31525, -31580, -31633, -31684, -31735,
   -31785, -31833, -31880, -31926, -31970, -32014, -32056, -32097,
   -32137, -32176, -32213, -32249, -32284, -32318, -32350, -32382,
   -32412, -32441, -32468, -32495, -32520, -32544, -32567, -32588,
   -32609, -32628, -32646, -32662, -32678, -32692, -32705, -32717,
   -32727, -32736, -32744, -32751, -32757, -32761, -32764, -32766
};


static int16_t bitrev[FFT_SIZE] =
{
	0x000, 0x200, 0x100, 0x300, 0x080, 0x280, 0x180, 0x380, 0x040, 0x240, 0x140, 0x340, 0x0c0, 0x2c0, 0x1c0, 0x3c0,
	0x020, 0x220, 0x120, 0x320, 0x0a0, 0x2a0, 0x1a0, 0x3a0, 0x060, 0x260, 0x160, 0x360, 0x0e0, 0x2e0, 0x1e0, 0x3e0,
	0x010, 0x210, 0x110, 0x310, 0x090, 0x290, 0x190, 0x390, 0x050, 0x250, 0x150, 0x350, 0x0d0, 0x2d0, 0x1d0, 0x3d0,
	0x030, 0x230, 0x130, 0x330, 0x0b0, 0x2b0, 0x1b0, 0x3b0, 0x070, 0x270, 0x170, 0x370, 0x0f0, 0x2f0, 0x1f0, 0x3f0,
	0x008, 0x208, 0x108, 0x308, 0x088, 0x288, 0x188, 0x388, 0x048, 0x248, 0x148, 0x348, 0x0c8, 0x2c8, 0x1c8, 0x3c8,
	0x028, 0x228, 0x128, 0x328, 0x0a8, 0x2a8, 0x1a8, 0x3a8, 0x068, 0x268, 0x168, 0x368, 0x0e8, 0x2e8, 0x1e8, 0x3e8,
	0x018, 0x218, 0x118, 0x318, 0x098, 0x298, 0x198, 0x398, 0x058, 0x258, 0x158, 0x358, 0x0d8, 0x2d8, 0x1d8, 0x3d8,
	0x038, 0x238, 0x138, 0x338, 0x0b8, 0x2b8, 0x1b8, 0x3b8, 0x078, 0x278, 0x178, 0x378, 0x0f8, 0x2f8, 0x1f8, 0x3f8,
	0x004, 0x204, 0x104, 0x304, 0x084, 0x284, 0x184, 0x384, 0x044, 0x244, 0x144, 0x344, 0x0c4, 0x2c4, 0x1c4, 0x3c4,
	0x024, 0x224, 0x124, 0x324, 0x0a4, 0x2a4, 0x1a4, 0x3a4, 0x064, 0x264, 0x164, 0x364, 0x0e4, 0x2e4, 0x1e4, 0x3e4,
	0x014, 0x214, 0x114, 0x314, 0x094, 0x294, 0x194, 0x394, 0x054, 0x254, 0x154, 0x354, 0x0d4, 0x2d4, 0x1d4, 0x3d4,
	0x034, 0x234, 0x134, 0x334, 0x0b4, 0x2b4, 0x1b4, 0x3b4, 0x074, 0x274, 0x174, 0x374, 0x0f4, 0x2f4, 0x1f4, 0x3f4,
	0x00c, 0x20c, 0x10c, 0x30c, 0x08c, 0x28c, 0x18c, 0x38c, 0x04c, 0x24c, 0x14c, 0x34c, 0x0cc, 0x2cc, 0x1cc, 0x3cc,
	0x02c, 0x22c, 0x12c, 0x32c, 0x0ac, 0x2ac, 0x1ac, 0x3ac, 0x06c, 0x26c, 0x16c, 0x36c, 0x0ec, 0x2ec, 0x1ec, 0x3ec,
	0x01c, 0x21c, 0x11c, 0x31c, 0x09c, 0x29c, 0x19c, 0x39c, 0x05c, 0x25c, 0x15c, 0x35c, 0x0dc, 0x2dc, 0x1dc, 0x3dc,
	0x03c, 0x23c, 0x13c, 0x33c, 0x0bc, 0x2bc, 0x1bc, 0x3bc, 0x07c, 0x27c, 0x17c, 0x37c, 0x0fc, 0x2fc, 0x1fc, 0x3fc,
	0x002, 0x202, 0x102, 0x302, 0x082, 0x282, 0x182, 0x382, 0x042, 0x242, 0x142, 0x342, 0x0c2, 0x2c2, 0x1c2, 0x3c2,
	0x022, 0x222, 0x122, 0x322, 0x0a2, 0x2a2, 0x1a2, 0x3a2, 0x062, 0x262, 0x162, 0x362, 0x0e2, 0x2e2, 0x1e2, 0x3e2,
	0x012, 0x212, 0x112, 0x312, 0x092, 0x292, 0x192, 0x392, 0x052, 0x252, 0x152, 0x352, 0x0d2, 0x2d2, 0x1d2, 0x3d2,
	0x032, 0x232, 0x132, 0x332, 0x0b2, 0x2b2, 0x1b2, 0x3b2, 0x072, 0x272, 0x172, 0x372, 0x0f2, 0x2f2, 0x1f2, 0x3f2,
	0x00a, 0x20a, 0x10a, 0x30a, 0x08a, 0x28a, 0x18a, 0x38a, 0x04a, 0x24a, 0x14a, 0x34a, 0x0ca, 0x2ca, 0x1ca, 0x3ca,
	0x02a, 0x22a, 0x12a, 0x32a, 0x0aa, 0x2aa, 0x1aa, 0x3aa, 0x06a, 0x26a, 0x16a, 0x36a, 0x0ea, 0x2ea, 0x1ea, 0x3ea,
	0x01a, 0x21a, 0x11a, 0x31a, 0x09a, 0x29a, 0x19a, 0x39a, 0x05a, 0x25a, 0x15a, 0x35a, 0x0da, 0x2da, 0x1da, 0x3da,
	0x03a, 0x23a, 0x13a, 0x33a, 0x0ba, 0x2ba, 0x1ba, 0x3ba, 0x07a, 0x27a, 0x17a, 0x37a, 0x0fa, 0x2fa, 0x1fa, 0x3fa,
	0x006, 0x206, 0x106, 0x306, 0x086, 0x286, 0x186, 0x386, 0x046, 0x246, 0x146, 0x346, 0x0c6, 0x2c6, 0x1c6, 0x3c6,
	0x026, 0x226, 0x126, 0x326, 0x0a6, 0x2a6, 0x1a6, 0x3a6, 0x066, 0x266, 0x166, 0x366, 0x0e6, 0x2e6, 0x1e6, 0x3e6,
	0x016, 0x216, 0x116, 0x316, 0x096, 0x296, 0x196, 0x396, 0x056, 0x256, 0x156, 0x356, 0x0d6, 0x2d6, 0x1d6, 0x3d6,
	0x036, 0x236, 0x136, 0x336, 0x0b6, 0x2b6, 0x1b6, 0x3b6, 0x076, 0x276, 0x176, 0x376, 0x0f6, 0x2f6, 0x1f6, 0x3f6,
	0x00e, 0x20e, 0x10e, 0x30e, 0x08e, 0x28e, 0x18e, 0x38e, 0x04e, 0x24e, 0x14e, 0x34e, 0x0ce, 0x2ce, 0x1ce, 0x3ce,
	0x02e, 0x22e, 0x12e, 0x32e, 0x0ae, 0x2ae, 0x1ae, 0x3ae, 0x06e, 0x26e, 0x16e, 0x36e, 0x0ee, 0x2ee, 0x1ee, 0x3ee,
	0x01e, 0x21e, 0x11e, 0x31e, 0x09e, 0x29e, 0x19e, 0x39e, 0x05e, 0x25e, 0x15e, 0x35e, 0x0de, 0x2de, 0x1de, 0x3de,
	0x03e, 0x23e, 0x13e, 0x33e, 0x0be, 0x2be, 0x1be, 0x3be, 0x07e, 0x27e, 0x17e, 0x37e, 0x0fe, 0x2fe, 0x1fe, 0x3fe,
	0x001, 0x201, 0x101, 0x301, 0x081, 0x281, 0x181, 0x381, 0x041, 0x241, 0x141, 0x341, 0x0c1, 0x2c1, 0x1c1, 0x3c1,
	0x021, 0x221, 0x121, 0x321, 0x0a1, 0x2a1, 0x1a1, 0x3a1, 0x061, 0x261, 0x161, 0x361, 0x0e1, 0x2e1, 0x1e1, 0x3e1,
	0x011, 0x211, 0x111, 0x311, 0x091, 0x291, 0x191, 0x391, 0x051, 0x251, 0x151, 0x351, 0x0d1, 0x2d1, 0x1d1, 0x3d1,
	0x031, 0x231, 0x131, 0x331, 0x0b1, 0x2b1, 0x1b1, 0x3b1, 0x071, 0x271, 0x171, 0x371, 0x0f1, 0x2f1, 0x1f1, 0x3f1,
	0x009, 0x209, 0x109, 0x309, 0x089, 0x289, 0x189, 0x389, 0x049, 0x249, 0x149, 0x349, 0x0c9, 0x2c9, 0x1c9, 0x3c9,
	0x029, 0x229, 0x129, 0x329, 0x0a9, 0x2a9, 0x1a9, 0x3a9, 0x069, 0x269, 0x169, 0x369, 0x0e9, 0x2e9, 0x1e9, 0x3e9,
	0x019, 0x219, 0x119, 0x319, 0x099, 0x299, 0x199, 0x399, 0x059, 0x259, 0x159, 0x359, 0x0d9, 0x2d9, 0x1d9, 0x3d9,
	0x039, 0x239, 0x139, 0x339, 0x0b9, 0x2b9, 0x1b9, 0x3b9, 0x079, 0x279, 0x179, 0x379, 0x0f9, 0x2f9, 0x1f9, 0x3f9,
	0x005, 0x205, 0x105, 0x305, 0x085, 0x285, 0x185, 0x385, 0x045, 0x245, 0x145, 0x345, 0x0c5, 0x2c5, 0x1c5, 0x3c5,
	0x025, 0x225, 0x125, 0x325, 0x0a5, 0x2a5, 0x1a5, 0x3a5, 0x065, 0x265, 0x165, 0x365, 0x0e5, 0x2e5, 0x1e5, 0x3e5,
	0x015, 0x215, 0x115, 0x315, 0x095, 0x295, 0x195, 0x395, 0x055, 0x255, 0x155, 0x355, 0x0d5, 0x2d5, 0x1d5, 0x3d5,
	0x035, 0x235, 0x135, 0x335, 0x0b5, 0x2b5, 0x1b5, 0x3b5, 0x075, 0x275, 0x175, 0x375, 0x0f5, 0x2f5, 0x1f5, 0x3f5,
	0x00d, 0x20d, 0x10d, 0x30d, 0x08d, 0x28d, 0x18d, 0x38d, 0x04d, 0x24d, 0x14d, 0x34d, 0x0cd, 0x2cd, 0x1cd, 0x3cd,
	0x02d, 0x22d, 0x12d, 0x32d, 0x0ad, 0x2ad, 0x1ad, 0x3ad, 0x06d, 0x26d, 0x16d, 0x36d, 0x0ed, 0x2ed, 0x1ed, 0x3ed,
	0x01d, 0x21d, 0x11d, 0x31d, 0x09d, 0x29d, 0x19d, 0x39d, 0x05d, 0x25d, 0x15d, 0x35d, 0x0dd, 0x2dd, 0x1dd, 0x3dd,
	0x03d, 0x23d, 0x13d, 0x33d, 0x0bd, 0x2bd, 0x1bd, 0x3bd, 0x07d, 0x27d, 0x17d, 0x37d, 0x0fd, 0x2fd, 0x1fd, 0x3fd,
	0x003, 0x203, 0x103, 0x303, 0x083, 0x283, 0x183, 0x383, 0x043, 0x243, 0x143, 0x343, 0x0c3, 0x2c3, 0x1c3, 0x3c3,
	0x023, 0x223, 0x123, 0x323, 0x0a3, 0x2a3, 0x1a3, 0x3a3, 0x063, 0x263, 0x163, 0x363, 0x0e3, 0x2e3, 0x1e3, 0x3e3,
	0x013, 0x213, 0x113, 0x313, 0x093, 0x293, 0x193, 0x393, 0x053, 0x253, 0x153, 0x353, 0x0d3, 0x2d3, 0x1d3, 0x3d3,
	0x033, 0x233, 0x133, 0x333, 0x0b3, 0x2b3, 0x1b3, 0x3b3, 0x073, 0x273, 0x173, 0x373, 0x0f3, 0x2f3, 0x1f3, 0x3f3,
	0x00b, 0x20b, 0x10b, 0x30b, 0x08b, 0x28b, 0x18b, 0x38b, 0x04b, 0x24b, 0x14b, 0x34b, 0x0cb, 0x2cb, 0x1cb, 0x3cb,
	0x02b, 0x22b, 0x12b, 0x32b, 0x0ab, 0x2ab, 0x1ab, 0x3ab, 0x06b, 0x26b, 0x16b, 0x36b, 0x0eb, 0x2eb, 0x1eb, 0x3eb,
	0x01b, 0x21b, 0x11b, 0x31b, 0x09b, 0x29b, 0x19b, 0x39b, 0x05b, 0x25b, 0x15b, 0x35b, 0x0db, 0x2db, 0x1db, 0x3db,
	0x03b, 0x23b, 0x13b, 0x33b, 0x0bb, 0x2bb, 0x1bb, 0x3bb, 0x07b, 0x27b, 0x17b, 0x37b, 0x0fb, 0x2fb, 0x1fb, 0x3fb,
	0x007, 0x207, 0x107, 0x307, 0x087, 0x287, 0x187, 0x387, 0x047, 0x247, 0x147, 0x347, 0x0c7, 0x2c7, 0x1c7, 0x3c7,
	0x027, 0x227, 0x127, 0x327, 0x0a7, 0x2a7, 0x1a7, 0x3a7, 0x067, 0x267, 0x167, 0x367, 0x0e7, 0x2e7, 0x1e7, 0x3e7,
	0x017, 0x217, 0x117, 0x317, 0x097, 0x297, 0x197, 0x397, 0x057, 0x257, 0x157, 0x357, 0x0d7, 0x2d7, 0x1d7, 0x3d7,
	0x037, 0x237, 0x137, 0x337, 0x0b7, 0x2b7, 0x1b7, 0x3b7, 0x077, 0x277, 0x177, 0x377, 0x0f7, 0x2f7, 0x1f7, 0x3f7,
	0x00f, 0x20f, 0x10f, 0x30f, 0x08f, 0x28f, 0x18f, 0x38f, 0x04f, 0x24f, 0x14f, 0x34f, 0x0cf, 0x2cf, 0x1cf, 0x3cf,
	0x02f, 0x22f, 0x12f, 0x32f, 0x0af, 0x2af, 0x1af, 0x3af, 0x06f, 0x26f, 0x16f, 0x36f, 0x0ef, 0x2ef, 0x1ef, 0x3ef,
	0x01f, 0x21f, 0x11f, 0x31f, 0x09f, 0x29f, 0x19f, 0x39f, 0x05f, 0x25f, 0x15f, 0x35f, 0x0df, 0x2df, 0x1df, 0x3df,
	0x03f, 0x23f, 0x13f, 0x33f, 0x0bf, 0x2bf, 0x1bf, 0x3bf, 0x07f, 0x27f, 0x17f, 0x37f, 0x0ff, 0x2ff, 0x1ff, 0x3ff
};


/** FIX_MPY() **/
/*
 * Assume Q(0,15) notation, 1 sign, 0 int, 15 frac bits
 */
int16_t __not_in_flash_func(FIX_MPY)(int16_t a, int16_t b)								        // Fixed-point mpy and scaling
{
	int32_t c;

	c = (int32_t)a * (int32_t)b;											// multiply 
	c = c + 0x4000;															// and round up
	c = c >>15;														        // Shift right fractional bits
	return((int16_t)c);													    // Return scaled product
}


/** FIX_FFT() **/
/*
 * fr[] 	i samples [1024]
 * fi[] 	q samples [1024]
 * inverse	true: iFFT
 * Note: i-FFT could also be calculated by exchanging the arrays for FFT (fxtbook.pdf 21.7)
 */
int __not_in_flash_func(fix_fft)(int16_t *fr, int16_t *fi, bool inverse)
{
	uint16_t i, j, m, k, step, scale;
	bool shift;
	int16_t qr, qi, tr, ti, wr, wi;
	int16_t *bp;

	/* Decimation in time: re-order samples */
	bp=&bitrev[0];
	for (i=0; i<FFT_SIZE; i++)
	{
		if (*bp > i)
		{
			tr = fr[i]; fr[i] = fr[*bp]; fr[*bp] = tr;
			ti = fi[i]; fi[i] = fi[*bp]; fi[*bp] = ti;
		}
		bp++;
	}


	scale = 0;
	step  = 1;																// Counting up: 1, 2, 4, 8, ...
	/* FFT Stages */
	for (k=FFT_ORDER; k>0; k--)                                             // #cycles: FFT_ORDER
	{
		/* Scaling
		 * Variable scaling, depends on current data
		 * --> it seems quite CPU intensive to go through complete array
		 *     FFT_ORDER times, could this be optimized?
		 * If always scaling:
		 * --> the main loop has log_2(FFT_SIZE) cycles,
		 *     resulting in an overall factor of 1/FFT_SIZE,
		 *     distributed over cycles to maximize accuracy.
		 */
        shift = false;														// No shift, unless...
        for (i=0; i<FFT_SIZE; ++i) 											// Range test all samples
        {
            if ((fr[i] > 0x3fff) || (fr[i] < -0x4000) || (fi[i] > 0x3fff) || (fi[i] < -0x4000))
            {
                shift = true;
                scale++;
                break;														// Bail out at first detect
            }
        }

		/* Inner loops resolving the butterflies for each stage*/
		for (m=0; m<step; m++) 											    // #cycles: step
		{
		    // Determine wiggle factors
			j = m << (k-1);													// 0 <= j < FFT_SIZE/2
			wr = Sine[j+FFT_SIZE/4];										// Real part, i.e. Cosine
			wi = inverse ? Sine[j] : -Sine[j];								// Imaginary part
			if (shift) { wr = wr/2; wi = wi/2; }							// Scale factors by 1/2

			for (i=m; i<FFT_SIZE; i+=(step*2))								// #cycles: FFT_SIZE/step
			{
				j = i + step;                                               // re-assign j !
				tr = FIX_MPY(wr,fr[j]) - FIX_MPY(wi,fi[j]);					// Complex multiply
				ti = FIX_MPY(wr,fi[j]) + FIX_MPY(wi,fr[j]);
				if (shift)
                    { qr = fr[i]/2; qi = fi[i]/2; }
                else
                    { qr = fr[i]; qi = fi[i]; }
				fr[i] = qr + tr;
				fi[i] = qi + ti;
				fr[j] = qr - tr;
				fi[j] = qi - ti;
			}																// #total: FFT_ORDER * step * FFT_SIZE/step
		}
		
		step = step<<1;
	}
	return scale;
}


#ifdef BLAH
// int16_t contains signed fixed point representation Q(1,14)
// 1 sign bit, 1 int bit and 14 frac bits
// precomputed value K represents 0.5
#define Q	14
#define K   (1 << (Q - 1))

// a + b
int16_t q_add(int16_t a, int16_t b)
{
    int32_t tmp;

    tmp = (int32_t)a + (int32_t)b;
	
    if (tmp > 0x7FFF) 														// Clip result
		tmp = 0x7FFF;
    else if (tmp < -0x8000) 
		tmp = -0x8000;

    return (int16_t)tmp;
}

// a - b
int16_t q_sub(int16_t a, int16_t b)
{
    return a - b;
}

// a * b
int16_t q_mul(int16_t a, int16_t b)
{
    int32_t tmp;

    tmp = (int32_t)a * (int32_t)b;
    tmp += K;    															// Rounding; mid values are rounded up
	tmp = tmp >> Q;    														// Correct by dividing by base
	
	if (tmp > 0x7FFF) 														// Clip result
		tmp = 0x7FFF;
	else if (tmp < -0x8000) 
		tmp = -0x8000;
	
	return (int16_t)tmp;
}

// a / b
int16_t q_div(int16_t a, int16_t b)
{
    int32_t tmp;

	tmp = (int32_t)a << Q;													// Pre multiply with base

    if ((tmp >= 0 && b >= 0) || (tmp < 0 && b < 0))  						// Rounding; mid values are rounded up 
        tmp += (b >> 2);
    else																	//  or down...
        tmp -= (b >> 2);

    return (int16_t)(tmp / b);
}
#endif
