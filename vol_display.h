#ifndef __VOL_DISPLAY_H__
#define __VOL_DISPLAY_H__

#define VERSION 0.1

#define BITS_PER_LONG (sizeof(long) * 8)
#ifndef NBITS
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#endif
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

#define KEY_MUTE_TOGGLE 113
#define KEY_VOL_DOWN 114
#define KEY_VOL_UP 115

#endif
