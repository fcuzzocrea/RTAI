/*
 * Copyright (C) 2002 Thomas Leibner (leibner@t-online.de) (first complete writeup)
 *               2002 David Schleef (ds@schleef.org) (COMEDI master)
 *               2002 Lorenzo Dozio (dozio@aero.polimi.it) (made it all work)
 *               2002-2008 Paolo Mantegazza (mantegazza@aero.polimi.it) (hints/support)
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

#define FUN_COMEDI_LXRT_INDX  9

#define _KCOMEDI_OPEN 			 0
#define _KCOMEDI_CLOSE 			 1
#define _KCOMEDI_LOCK 			 2
#define _KCOMEDI_UNLOCK 		 3
#define _KCOMEDI_CANCEL 		 4
#define _KCOMEDI_REGISTER_CALLBACK 	 5
#define _KCOMEDI_COMMAND 		 6
#define _KCOMEDI_COMMAND_TEST 		 7

/* DEPRECATED function */
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

#ifdef __KERNEL__ /* For kernel module build. */

#include <linux/comedilib.h>
//#include <linux/comedidev.h>

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

RTAI_SYSCALL_MODE long rt_comedi_command_data_read (void *dev, unsigned int subdev, long nsampl, lsampl_t *data);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread(void *dev, unsigned int subdev, long nsampl, lsampl_t *data, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_if(void *dev, unsigned int subdev, long nsampl, lsampl_t *data, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_until(void *dev, unsigned int subdev, long nsampl, lsampl_t *data, RTIME until, long *mask);

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_timed(void *dev, unsigned int subdev, long nsampl, lsampl_t *data, RTIME delay, long *cbmask);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else  /* __KERNEL__ not defined */

#include <string.h>
#include <linux/comedi.h>

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
		memcpy(&lcmd, cmd, sizeof(comedi_cmd));
        	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMMAND, &arg).i[LOW];
		if (!retval) {
			memcpy(cmd, &lcmd, sizeof(comedi_cmd));
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
		memcpy(&lcmd, cmd, sizeof(comedi_cmd));
	        retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMMAND_TEST, &arg).i[LOW];
		if (!retval) {
			memcpy(cmd, &lcmd, sizeof(comedi_cmd));
		}
        	return retval;
	}
	return -1;
}

RTAI_PROTO(int, comedi_trigger, (void *dev, unsigned int subdev, comedi_trig *it))
{
	return -1;
}

RTAI_PROTO(long, rt_comedi_command_data_read, (void *dev, unsigned int subdev, long nsampl, lsampl_t *data))
{
	int retval;
	lsampl_t ldata[nsampl];
	struct { void *dev; unsigned long subdev; long nsampl; lsampl_t *data; } arg = { dev, subdev, nsampl, ldata };
	retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_READ, &arg).i[LOW];
	memcpy(data, &ldata, nsampl*sizeof(lsampl_t));
	return retval;
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
		comedi_insn linsn = *insn;
		lsampl_t ldata[linsn.n];
	        struct { void *dev; comedi_insn *insn; } arg = { dev, &linsn };
		int retval;
		memcpy(ldata, linsn.data, linsn.n*sizeof(lsampl_t));
		linsn.data = ldata;
		if (!(retval = rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_DO_INSN, &arg).i[LOW])) {
			memcpy(insn[0].data, ldata, linsn.n*sizeof(lsampl_t));
		}
        	return retval;
	}
	return -1;
}

RTAI_PROTO(int, rt_comedi_do_insnlist, (void *dev, comedi_insnlist *ilist))
{
	if (ilist) {
		comedi_insnlist lilist = *ilist;
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
	memcpy(krange, &lkrange, sizeof(comedi_krange));
	return retval;
}

RTAI_PROTO(long, rt_comedi_command_data_wread, (void *dev, unsigned int subdev, long nsampl, lsampl_t *data, long *mask))
{
	struct { void *dev; unsigned long subdev; long nsampl; lsampl_t *data; long *mask; } arg = { dev, subdev, nsampl, data, mask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_command_data_wread_if, (void *dev, unsigned int subdev, long nsampl, lsampl_t *data, long *mask))
{
	struct { void *dev; unsigned long subdev; long nsampl; lsampl_t *data; long *mask; } arg = { dev, subdev, nsampl, data, mask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_IF, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_command_data_wread_until, (void *dev, unsigned int subdev, long nsampl, lsampl_t *data, RTIME until, long *mask))
{
	struct { void *dev; unsigned long subdev; long nsampl; lsampl_t *data; RTIME until; long *mask; } arg = { dev, subdev, nsampl, data, until, mask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_UNTIL, &arg).i[LOW];
}

RTAI_PROTO(long, rt_comedi_command_data_wread_timed, (void *dev, unsigned int subdev, long nsampl, lsampl_t *data, RTIME delay, long *mask))
{
	struct { void *dev; unsigned long subdev; long nsampl; lsampl_t *data; RTIME delay; long *mask; } arg = { dev, subdev, nsampl, data, delay, mask };
	return rtai_lxrt(FUN_COMEDI_LXRT_INDX, COMEDI_LXRT_SIZARG, _KCOMEDI_COMD_DATA_WREAD_TIMED, &arg).i[LOW];
}

#endif /* #ifdef __KERNEL__ */

#define comedi_register_callback(dev, subdev, mask, cb, arg)  rt_comedi_register_callback(dev, subdev, mask, NULL, arg)

#endif /* #ifndef _RTAI_COMEDI_H_ */
