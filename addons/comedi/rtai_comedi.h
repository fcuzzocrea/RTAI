/*
 * Copyright (C) 2002 Thomas Leibner (leibner@t-online.de) (first complete writeup)
 *               2002 David Schleef (ds@schleef.org) (COMEDI master)
 *               2002 Lorenzo Dozio (dozio@aero.polimi.it) (made it all work)
 *               2002-2010 Paolo Mantegazza (mantegazza@aero.polimi.it) (hints/support)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */


#ifndef _RTAI_COMEDI_H_
#define _RTAI_COMEDI_H_

#include <rtai_types.h>
#include <rtai_sem.h>
#include <rtai_netrpc.h>

#ifdef CONFIG_RTAI_USE_LINUX_COMEDI
// undef RTAI VERSION to avoid conflicting with the same macro in COMEDI
#undef VERSION
#endif

#define FUN_COMEDI_LXRT_INDX  9

#define _KCOMEDI_OPEN 			 0
#define _KCOMEDI_CLOSE 			 1
#define _KCOMEDI_LOCK 			 2
#define _KCOMEDI_UNLOCK 		 3
#define _KCOMEDI_CANCEL 		 4
#define _KCOMEDI_REGISTER_CALLBACK 	 5
#define _KCOMEDI_COMMAND 		 6
#define _KCOMEDI_COMMAND_TEST 		 7
#define _KCOMEDI_TRIGGER 		 8
#define _KCOMEDI_DATA_WRITE 		 9
#define _KCOMEDI_DATA_READ 		10
#define _KCOMEDI_DATA_READ_DELAYED	11
#define _KCOMEDI_DATA_READ_HINT         12
#define _KCOMEDI_DIO_CONFIG 		13
#define _KCOMEDI_DIO_READ 		14
#define _KCOMEDI_DIO_WRITE 		15
#define _KCOMEDI_DIO_BITFIELD 		16
#define _KCOMEDI_GET_N_SUBDEVICES 	17
#define _KCOMEDI_GET_VERSION_CODE 	18
#define _KCOMEDI_GET_DRIVER_NAME 	19
#define _KCOMEDI_GET_BOARD_NAME 	20
#define _KCOMEDI_GET_SUBDEVICE_TYPE 	21
#define _KCOMEDI_FIND_SUBDEVICE_TYPE	22
#define _KCOMEDI_GET_N_CHANNELS 	23
#define _KCOMEDI_GET_MAXDATA 		24
#define _KCOMEDI_GET_N_RANGES 		25
#define _KCOMEDI_DO_INSN 		26
#define _KCOMEDI_DO_INSN_LIST		27
#define _KCOMEDI_POLL 			28

/* DEPRECATED function
#define _KCOMEDI_GET_RANGETYPE 		29
*/

/* ALPHA functions */
#define _KCOMEDI_GET_SUBDEVICE_FLAGS 	30
#define _KCOMEDI_GET_LEN_CHANLIST 	31
#define _KCOMEDI_GET_KRANGE 		32
#define _KCOMEDI_GET_BUF_HEAD_POS	33
#define _KCOMEDI_SET_USER_INT_COUNT	34
#define _KCOMEDI_MAP 			35
#define _KCOMEDI_UNMAP 			36

/* RTAI specific callbacks from kcomedi to user space */
#define _KCOMEDI_WAIT        		37
#define _KCOMEDI_WAIT_IF     		38
#define _KCOMEDI_WAIT_UNTIL  		39
#define _KCOMEDI_WAIT_TIMED  		40

/* RTAI specific functions to allocate/free comedi_cmd */
#define _KCOMEDI_ALLOC_CMD  		41
#define _KCOMEDI_FREE_CMD  		42

#define _KCOMEDI_COMD_DATA_READ         43
#define _KCOMEDI_COMD_DATA_WREAD        44
#define _KCOMEDI_COMD_DATA_WREAD_IF     45
#define _KCOMEDI_COMD_DATA_WREAD_UNTIL  46
#define _KCOMEDI_COMD_DATA_WREAD_TIMED  47
#define _KCOMEDI_COMD_DATA_WRITE        48

#ifdef CONFIG_RTAI_USE_LINUX_COMEDI
typedef unsigned int lsampl_t;
typedef unsigned short sampl_t;
typedef struct comedi_cmd comedi_cmd;
typedef struct comedi_insn comedi_insn;
typedef struct comedi_insnlist comedi_insnlist;
typedef struct comedi_krange comedi_krange;
#endif

#include <linux/comedi.h>

#ifdef __KERNEL__ /* For kernel module build. */

#include <linux/comedilib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

RTAI_SYSCALL_MODE long rt_comedi_wait(long *cbmask);

RTAI_SYSCALL_MODE long rt_comedi_wait_if(long *cbmask);

RTAI_SYSCALL_MODE long rt_comedi_wait_until(RTIME until, long *cbmask);

RTAI_SYSCALL_MODE long rt_comedi_wait_timed(RTIME delay, long *cbmask);

RTAI_SYSCALL_MODE char *rt_comedi_get_driver_name(void *dev, char *name);

RTAI_SYSCALL_MODE char *rt_comedi_get_board_name(void *dev, char *name);

RTAI_SYSCALL_MODE int rt_comedi_register_callback(void *dev, unsigned int subdev, unsigned int mask, int (*callback)(unsigned int, void *), void *arg);

RTAI_SYSCALL_MODE long rt_comedi_command_data_read (void *dev, unsigned int subdev, long nchans, lsampl_t *data);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread(void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_if(void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_until(void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME until, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_timed(void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME delay, long *cbmask);

RTAI_SYSCALL_MODE int rt_comedi_do_insnlist(void *dev, comedi_insnlist *ilist);

RTAI_SYSCALL_MODE int rt_comedi_trigger(void *dev, unsigned int subdev);

RTAI_SYSCALL_MODE long rt_comedi_command_data_write(void *dev, unsigned int subdev, long nchans, lsampl_t *data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else  /* __KERNEL__ not defined */

#include <string.h>
#include <asm/rtai_lxrt.h>
#include <rtai_msg.h>
#include <rtai_shm.h>

typedef void comedi_t;

#define COMEDI_LXRT_SIZARG sizeof(arg)

RTAI_PROTO(void *, comedi_open, (const char *filename))
{
	char lfilename[COMEDI_NAMELEN];
        struct { char *minor; } arg = { lfilename };
	strncpy(lfilename, filename, COMEDI_NAMELEN - 1);
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_OPEN, &arg).v[LOW];
}

RTAI_PROTO(int, comedi_close, (void *dev))
{
        struct { void *dev; } arg = { dev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_CLOSE, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_lock, (void *dev, unsigned int subdev))
{
        struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_LOCK, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_unlock, (void *dev, unsigned int subdev))
{
        struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_UNLOCK, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_cancel, (void *dev, unsigned int subdev))
{
        struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_CANCEL, &arg).i[LOW];
}

RTAI_PROTO(int, rt_comedi_register_callback, (void *dev, unsigned int subdevice, unsigned int mask, int (*cb)(unsigned int, void *), void *task))
{
        struct { void *dev; unsigned long subdevice; unsigned long mask; void *cb; void *task; } arg = { dev, subdevice, mask, NULL, task };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_REGISTER_CALLBACK, &arg).i[LOW];
}

#define comedi_register_callback(dev, subdev, mask, cb, arg)  rt_comedi_register_callback(dev, subdev, mask, NULL, arg)

RTAI_PROTO(long, rt_comedi_wait, (long *cbmask))
{
       	struct { long *cbmask; } arg = { cbmask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_WAIT, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_wait_if, (long *cbmask))
{
       	struct { long *cbmask; } arg = { cbmask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_WAIT_IF, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_wait_until, (RTIME until, long *cbmask))
{
       	struct { RTIME until; long *cbmask; } arg = { until, cbmask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_WAIT_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_wait_timed, (RTIME delay, long *cbmask))
{
       	struct { RTIME delay; long *cbmask; } arg = { delay, cbmask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_WAIT_TIMED, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_command, (void *dev, comedi_cmd *cmd))
{
	comedi_cmd lcmd;
	int retval;
        struct { void *dev; comedi_cmd *cmd; } arg = { dev, &lcmd };
	if (cmd) {
		lcmd = cmd[0];
        	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMMAND, &arg).i[LOW];
		if (!retval) {
			cmd[0] = lcmd;
		}
        	return retval;
	}
	return -1;
}

RTAI_PROTO(int, comedi_command_test, (void *dev, comedi_cmd *cmd))
{
	comedi_cmd lcmd;
	int retval;
        struct { void *dev; comedi_cmd *cmd; } arg = { dev, &lcmd };
	if (cmd) {
		lcmd = cmd[0];
	        retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMMAND_TEST, &arg).i[LOW];
		if (!retval) {
			cmd[0] = lcmd;
		}
        	return retval;
	}
	return -1;
}

RTAI_PROTO(long, rt_comedi_command_data_read, (void *dev, unsigned int subdev, long nchans, lsampl_t *data))
{
	int retval;
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; } arg = { dev, subdev, nchans, ldata };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_READ, &arg).i[LOW];
	memcpy(data, &ldata, sizeof(ldata));
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_write, (void *dev, unsigned int subdev, long nchans, lsampl_t *data))
{
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; } arg = { dev, subdev, nchans, ldata };
	memcpy(ldata, data, sizeof(ldata));
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WRITE, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_data_write, (void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t data))
{
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; long data; } arg = { dev, subdev, chan, range, aref, data };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DATA_WRITE, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_data_read, (void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data))
{
	int retval;
	lsampl_t ldata;
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; lsampl_t *data; } arg = { dev, subdev, chan, range, aref, &ldata };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DATA_READ, &arg).i[LOW];
	data[0] = ldata;
	return retval;
}

RTAI_PROTO(int, comedi_data_read_delayed, (void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data, unsigned int nanosec))
{
	int retval;
	lsampl_t ldata;
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; lsampl_t *data; unsigned long nanosec;} arg = { dev, subdev, chan, range, aref, &ldata, nanosec };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DATA_READ_DELAYED, &arg).i[LOW];
	data[0] = ldata;
	return retval;
}

RTAI_PROTO(int, comedi_data_read_hint, (void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref))
{
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref;} arg = { dev, subdev, chan, range, aref};
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DATA_READ_HINT, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_dio_config, (void *dev, unsigned int subdev, unsigned int chan, unsigned int io))
{
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long io; } arg = { dev, subdev, chan, io };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DIO_CONFIG, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_dio_read, (void *dev, unsigned int subdev, unsigned int chan, unsigned int *val))
{
        int retval;
	unsigned int lval;
        struct { void *dev; unsigned long subdev; unsigned long chan; unsigned int *val; } arg = { dev, subdev, chan, &lval };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DIO_READ, &arg).i[LOW];
	val[0] = lval;
	return retval;
}

RTAI_PROTO(int, comedi_dio_write, (void *dev, unsigned int subdev, unsigned int chan, unsigned int val))
{
        struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long val; } arg = { dev, subdev, chan, val };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DIO_WRITE, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_dio_bitfield, (void *dev, unsigned int subdev, unsigned int mask, unsigned int *bits))
{
        int retval;
	unsigned int lbits = bits[0];
	lbits = *bits;
        struct { void *dev; unsigned long subdev; unsigned long mask; unsigned int *bits; } arg = { dev, subdev, mask, &lbits };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DIO_BITFIELD, &arg).i[LOW];
	bits[0] = lbits;
	return retval;
}

RTAI_PROTO(int, comedi_get_n_subdevices, (void *dev))
{
	struct { void *dev; } arg = { dev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_N_SUBDEVICES, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_get_version_code, (void *dev))
{
	struct { void *dev;} arg = { dev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_VERSION_CODE, &arg).i[LOW];
}

RTAI_PROTO(char *, rt_comedi_get_driver_name, (void *dev, char *name))
{
        char lname[COMEDI_NAMELEN];
        struct { void *dev; char *name; } arg = { dev, lname };
	if ((char *)rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_DRIVER_NAME, &arg).v[LOW] == lname) {
		strncpy(name, lname, COMEDI_NAMELEN);
		return name;
	}
	return 0;
}

RTAI_PROTO(char *, rt_comedi_get_board_name, (void *dev, char *name))
{
        char lname[COMEDI_NAMELEN];
        struct { void *dev; char *name; } arg = { dev, lname };
	if ((char *)rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_BOARD_NAME, &arg).v[LOW] == lname) {
		strncpy(name, lname, COMEDI_NAMELEN);
		return name;
	}
	return 0;
}

RTAI_PROTO(int, comedi_get_subdevice_type, (void *dev, unsigned int subdev))
{
        struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_SUBDEVICE_TYPE, &arg).i[LOW];
}

RTAI_PROTO(unsigned int, comedi_get_subdevice_flags, (void *dev, unsigned int subdev))
{
	struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_SUBDEVICE_FLAGS, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_find_subdevice_by_type,(void *dev, int type, unsigned int subd))
{
        struct { void *dev; long type; unsigned long subd; } arg = { dev, type, subd };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_FIND_SUBDEVICE_TYPE, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_get_n_channels,(void *dev, unsigned int subdev))
{
        struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_N_CHANNELS, &arg).i[LOW];
}

RTAI_PROTO(lsampl_t, comedi_get_maxdata,(void *dev, unsigned int subdev, unsigned int chan))
{
        struct { void *dev; unsigned long subdev; unsigned long chan;} arg = { dev, subdev, chan };
        return (lsampl_t)rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_MAXDATA, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_get_n_ranges,(void *dev, unsigned int subdev, unsigned int chan))
{
        struct { void *dev; unsigned long subdev; unsigned long chan;} arg = { dev, subdev, chan };
        return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_N_RANGES, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_do_insn, (void *dev, comedi_insn *insn))
{
	if (insn) {
		comedi_insn linsn = insn[0];
		lsampl_t ldata[linsn.n];
	        struct { void *dev; comedi_insn *insn; } arg = { dev, &linsn };
		int retval;
		memcpy(ldata, linsn.data, sizeof(ldata));
		linsn.data = ldata;
		if ((retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DO_INSN, &arg).i[LOW]) >= 0) {
			memcpy(insn[0].data, ldata, sizeof(ldata));
		}
        	return retval;
	}
	return -1;
}

RTAI_PROTO(int, rt_comedi_do_insnlist, (void *dev, comedi_insnlist *ilist))
{
	if (ilist) {
		comedi_insnlist lilist = ilist[0];
		comedi_insn insns[lilist.n_insns];
	        struct { void *dev; comedi_insnlist *ilist; } arg = { dev, &lilist };
		int i, retval, maxdata;

		maxdata = 0;
		for (i = 0; i < lilist.n_insns; i++) { 
			if (lilist.insns[i].n > maxdata) {
				maxdata = lilist.insns[i].n;
			}
		} {
		lsampl_t ldata[lilist.n_insns][maxdata];
		for (i = 0; i < lilist.n_insns; i++) { 
			memcpy(ldata[i], lilist.insns[i].data, lilist.insns[i].n*sizeof(lsampl_t));
			insns[i] = lilist.insns[i];
		}
		lilist.insns = insns;
		if (!(retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DO_INSN_LIST, &arg).i[LOW])) {
			for (i = 0; i < retval; i++) { 
				memcpy(ilist->insns[i].data, ldata[i], insns[i].n*sizeof(lsampl_t));
			} 
		} }
        	return retval;
	}
	return -1;
}

RTAI_PROTO(int, rt_comedi_trigger, (void *dev, unsigned int subdev))
{
	struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_TRIGGER, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_poll,(void *dev, unsigned int subdev))
{
	struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_POLL, &arg).i[LOW];
}

RTAI_PROTO(int, comedi_get_krange,(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, comedi_krange *krange))
{
	int retval;
	comedi_krange lkrange;
	struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; comedi_krange *krange; } arg = { dev, subdev, chan, range, &lkrange };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_GET_KRANGE, &arg).i[LOW];
	krange[0] = lkrange;
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_wread, (void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask))
{
	int retval;
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; long *mask; } arg = { dev, subdev, nchans, ldata, mask };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD, &arg).i[LOW];
	memcpy(data, &ldata, sizeof(ldata));
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_wread_if, (void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask))
{
	int retval;
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; long *mask; } arg = { dev, subdev, nchans, ldata, mask };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_IF, &arg).i[LOW];
	memcpy(data, &ldata, sizeof(ldata));
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_wread_until, (void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME until, long *mask))
{
	int retval;
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; RTIME until; long *mask; } arg = { dev, subdev, nchans, ldata, until, mask };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_UNTIL, &arg).i[LOW];
	memcpy(data, &ldata, sizeof(ldata));
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_wread_timed, (void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME delay, long *mask))
{
	int retval;
	lsampl_t ldata[nchans];
	struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; RTIME delay; long *mask; } arg = { dev, subdev, nchans, ldata, delay, mask };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_TIMED, &arg).i[LOW];
	memcpy(data, &ldata, sizeof(ldata));
	return retval;
}

#if 0

static inline void *RT_comedi_open(unsigned long node, int port, const char *filename)
{
	if (node) {
		struct { const char *minor; long size; } arg = { filename, strlen(filename) };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_OPEN, 0), UR1(1, 2), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(UINT, SINT) };
                return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).v[LOW];
	}
	return comedi_open(filename);
}

static inline int RT_comedi_close(unsigned long node, int port, void *dev)
{
	if (node) {
        	struct { void *dev; } arg = { dev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_CLOSE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(VADR) };
                return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_close(dev);
}

static inline int RT_comedi_lock(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
      		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_LOCK, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
                return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_lock(dev, subdev);
}

static inline int RT_comedi_unlock(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
      		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_UNLOCK, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
                return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_unlock(dev, subdev);
}

static inline int RT_comedi_cancel(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
      		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_CANCEL, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
                return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_cancel(dev, subdev);
}

static inline int RT_comedi_register_callback(unsigned long node, int port, void *dev, unsigned int subdevice, unsigned int mask, int (*cb)(unsigned int, void *), void *task)
{
	if (node) {
		struct { void *dev; unsigned long subdevice; unsigned long mask; void *cb; void *task; } arg = { dev, subdevice, mask, NULL, NULL };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_REGISTER_CALLBACK, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES5(VADR, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_register_callback(dev, subdevice, mask, cb, task);
}

static inline long RT_comedi_wait(unsigned long node, int port, long *cbmask)
{
	if (node) {
		struct { long *cbmask; } arg = { cbmask };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_WAIT, 0), UW1(1, 0), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_wait(cbmask);
}

static inline long RT_comedi_wait_if(unsigned long node, int port, long *cbmask)
{
	if (node) {
		struct { long *cbmask; } arg = { cbmask };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_WAIT_IF, 0), UW1(1, 0), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_wait_if(cbmask);
}

static inline long RT_comedi_wait_until(unsigned long node, int port, RTIME until, long *cbmask)
{
	if (node) {
		struct { RTIME until; long *cbmask; } arg = { until, cbmask };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_WAIT_UNTIL, 1), UW1(2, 0), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(RTIM, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_wait_until(until, cbmask);
}

static inline long RT_comedi_wait_timed(unsigned long node, int port, RTIME delay, long *cbmask)
{
	if (node) {
		struct { RTIME delay; long *cbmask; } arg = { delay, cbmask };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_WAIT_TIMED, 1), UW1(2, 0), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(RTIM, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_wait_timed(delay, cbmask);
}

static inline int RT_comedi_command(unsigned long node, int port, void *dev, comedi_cmd *cmd)
{
	if (node) {
        	struct { void *dev; comedi_cmd *cmd; long cmdsize; } arg = { dev, cmd, sizeof(comedi_cmd) };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMMAND, 0), UR1(2, 3), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(RTIM, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_command(dev, cmd);
}

static inline int RT_comedi_command_test(unsigned long node, int port, void *dev, comedi_cmd *cmd)
{
	if (node) {
        	struct { void *dev; comedi_cmd *cmd; long cmdsize; } arg = { dev, cmd, sizeof(comedi_cmd) };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMMAND_TEST, 0), UR1(2, 3), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(RTIM, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_command_test(dev, cmd);
}

static inline long RT_comedi_command_data_read(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; } arg = { dev, subdev, nchans, data };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_READ, 0), UR1(2, 3), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_read(dev, subdev, nchans, data);
}

static inline long RT_comedi_command_data_write(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; } arg = { dev, subdev, nchans, data };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_WRITE, 0), UR1(2, 3), &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_read(dev, subdev, nchans, data);
}

static inline int RT_comedi_data_write(unsigned long node, long port, void *dev, unsigned long subdev, unsigned long chan, unsigned long range, unsigned long aref, long data)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; long data; } arg = { dev, subdev, chan, range, aref, data };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DATA_WRITE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES6(VADR, UINT, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_data_write(dev, subdev, chan, range, aref, data);
}

static inline int RT_comedi_data_read(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; lsampl_t *data; } arg = { dev, subdev, chan, range, aref, data };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DATA_READ, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES6(VADR, UINT, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_data_read(dev, subdev, chan, range, aref, data);
}

static inline int RT_comedi_data_read_delayed(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data, unsigned int nanosec)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; lsampl_t *data; unsigned long nanosec; } arg = { dev, subdev, chan, range, aref, data, nanosec };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DATA_READ_DELAYED, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES7(VADR, UINT, UINT, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_data_read_delayed(dev, subdev, chan, range, aref, data, nanosec);
}

static inline int RT_comedi_data_read_hint(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; unsigned long aref; } arg = { dev, subdev, chan, range, aref };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DATA_READ_HINT, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES5(VADR, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_data_read_hint(dev, subdev, chan, range, aref);
}

static inline int RT_comedi_dio_config(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int io)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long io; } arg = { dev, subdev, chan, io };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DIO_CONFIG, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_dio_config(dev, subdev, chan, io);
}

static inline int RT_comedi_dio_read(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int *val)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned int *val; } arg = { dev, subdev, chan, val };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DIO_READ, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_dio_read(dev, subdev, chan, val);
}

static inline int RT_comedi_dio_write(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int val)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; long val; } arg = { dev, subdev, chan, val };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DIO_WRITE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_dio_write(dev, subdev, chan, val);
}

static inline int RT_comedi_dio_bitfield(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int mask, unsigned int *bits)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long mask; unsigned int *bits; } arg = { dev, subdev, mask, bits };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DIO_BITFIELD, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES4(VADR, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_dio_bitfield(dev, subdev, mask, bits);
}

static inline int RT_comedi_get_n_subdevices(unsigned long node, int port, void *dev)
{
	if (node) {
		struct { void *dev; } arg = { dev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_N_SUBDEVICES, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(VADR) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_n_subdevices(dev);
}

static inline int RT_comedi_get_version_code(unsigned long node, int port, void *dev)
{
	if (node) {
		struct { void *dev; } arg = { dev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_VERSION_CODE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(VADR) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_version_code(dev);
}

static inline char *RT_comedi_get_driver_name(unsigned long node, int port, void *dev, char *name)
{
	if (node) {
		struct { void *dev; char *name; long size; } arg = { dev, name, COMEDI_NAMELEN };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_DRIVER_NAME, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES1(VADR) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).v[LOW];
	}
	return rt_comedi_get_driver_name(dev, name);
}

static inline char *RT_comedi_get_board_name(unsigned long node, int port, void *dev, char *name)
{
	if (node) {
		struct { void *dev; char *name; long size; } arg = { dev, name, COMEDI_NAMELEN };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_BOARD_NAME, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(VADR, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).v[LOW];
	}
	return rt_comedi_get_board_name(dev, name);
}

static inline int RT_comedi_get_subdevice_type(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_SUBDEVICE_TYPE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_subdevice_type(dev, subdev);
}

static inline int RT_comedi_get_subdevice_flags(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_SUBDEVICE_FLAGS, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_subdevice_flags(dev, subdev);
}

static inline int RT_comedi_find_subdevice_by_type(unsigned long node, int port, void *dev, int type, unsigned int subdev)
{
	if (node) {
		struct { void *dev; long type; unsigned long subdev; } arg = { dev, type, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_FIND_SUBDEVICE_TYPE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(VADR, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_find_subdevice_by_type(dev, type, subdev);
}

static inline int RT_comedi_get_n_channels(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_N_CHANNELS, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_n_channels(dev, subdev);
}

static inline lsampl_t RT_comedi_get_maxdata(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; } arg = { dev, subdev, chan };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_MAXDATA, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(VADR, UINT, UINT) };
		return (lsampl_t)rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_maxdata(dev, subdev, chan);
}

static inline int RT_comedi_get_n_ranges(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; } arg = { dev, subdev, chan };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_N_RANGES, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES3(VADR, UINT, UINT) };
		return (lsampl_t)rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_maxdata(dev, subdev, chan);
}

static inline int RT_comedi_do_insn(unsigned long node, int port, void *dev, comedi_insn *insn)
{
	if (node) {
		struct { void *dev; comedi_insn *insn; } arg = { dev, insn };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DO_INSN, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_do_insn(dev, insn);
}

static inline int RT_comedi_do_insnlist(unsigned long node, int port, void *dev, comedi_insnlist *ilist)
{
	if (node) {
		struct { void *dev; comedi_insnlist *ilist; } arg = { dev, ilist };
                struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_DO_INSN_LIST, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_do_insnlist(dev, ilist);
}

static inline int RT_comedi_trigger(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_TRIGGER, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_trigger(dev, subdev);
}

static inline int RT_comedi_poll(unsigned long node, int port, void *dev, unsigned int subdev)
{
	if (node) {
		struct { void *dev; unsigned long subdev; } arg = { dev, subdev };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_POLL, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES2(VADR, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_poll(dev, subdev);
}

static inline int RT_comedi_get_krange(unsigned long node, int port, void *dev, unsigned int subdev, unsigned int chan, unsigned int range, comedi_krange *krange)
{
	if (node) {
		struct { void *dev; unsigned long subdev; unsigned long chan; unsigned long range; comedi_krange *krange; } arg = { dev, subdev, chan, range, krange };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_GET_KRANGE, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES5(VADR, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return comedi_get_krange(dev, subdev, chan, range, krange);
}

static inline long RT_comedi_command_data_wread(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; long *mask; } arg = { dev, subdev, nchans, data, mask };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_WREAD, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES5(VADR, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_wread(dev, subdev, nchans, data, mask);
}

static inline long RT_comedi_command_data_wread_if(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *mask)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; long *mask; } arg = { dev, subdev, nchans, data, mask };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_WREAD_IF, 0), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES5(VADR, UINT, UINT, UINT, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_wread_if(dev, subdev, nchans, data, mask);
}

static inline long RT_comedi_command_data_wread_until(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME until, long *mask)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; RTIME until; long *mask; } arg = { dev, subdev, nchans, data, until, mask };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_WREAD_UNTIL, 5), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES6(VADR, UINT, UINT, UINT, RTIM, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_wread_until(dev, subdev, nchans, data, until, mask);
}

static inline long RT_comedi_command_data_wread_timed(unsigned long node, int port, void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME delay, long *mask)
{
	if (node) {
		struct { void *dev; unsigned long subdev; long nchans; lsampl_t *data; RTIME delay; long *mask; } arg = { dev, subdev, nchans, data, delay, mask };
		struct { unsigned long fun; long type; void *args; long argsize; long space; unsigned long partypes; } args = { PACKPORT(port, FUN_COMEDI_LXRT_INDX, _KCOMEDI_COMD_DATA_WREAD_TIMED, 5), 0, &arg, COMEDI_LXRT_SIZARG, 0, PARTYPES6(VADR, UINT, UINT, UINT, RTIM, UINT) };
		return rtai_lxrt(NET_RPC_IDX, SIZARGS, NETRPC, &args).i[LOW];
	}
	return rt_comedi_command_data_wread_timed(dev, subdev, nchans, data, delay, mask);
}

#endif

#endif /* #ifdef __KERNEL__ */

/* help macros, for the case one does not (want to) remember insn fields */

#define _BUILD_INSN(icode, instr, subdevice, datap, nd, chan, arange, aref) \
	do { \
		(instr).insn     = icode; \
		(instr).subdev   = subdevice; \
		(instr).n        = nd; \
		(instr).data     = datap; \
		(instr).chanspec = CR_PACK((chan), (arange), (aref)); \
	} while (0)

#define BUILD_AREAD_INSN(insn, subdev, data, nd, chan, arange, aref) \
	_BUILD_INSN(INSN_READ, (insn), subdev, &(data), nd, chan, arange, aref)

#define BUILD_AWRITE_INSN(insn, subdev, data, nd, chan, arange, aref) \
	_BUILD_INSN(INSN_WRITE, (insn), subdev, &(data), nd, chan, arange, aref)

#define BUILD_DIO_INSN(insn, subdev, data) \
	_BUILD_INSN(INSN_BITS, (insn), subdev, &(data), 2, 0, 0, 0)

#define BUILD_TRIG_INSN(insn, subdev, data) \
	_BUILD_INSN(INSN_INTTRIG, (insn), subdev, &(data), 1, 0, 0, 0)

#endif /* #ifndef _RTAI_COMEDI_H_ */
