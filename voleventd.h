#ifndef __VOLEVENTD_H__
#define __VOLEVENTD_H__

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "messages.h"

#define VERSION 0.9

#define BITS_PER_LONG (sizeof(long) * 8)
#ifndef NBITS
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#endif
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

//#define PID_FILE "voleventd.pid"
//#define VOLEVENTD_SOCKET "voleventd.socket"
#define PID_FILE "/var/run/voleventd.pid"
#define VOLEVENTD_SOCKET "/var/run/voleventd.socket"
#define SOCKET_PERMISSIONS (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)

#define KEY_MUTE_TOGGLE 113
#define KEY_VOL_DOWN 114
#define KEY_VOL_UP 115

#endif
