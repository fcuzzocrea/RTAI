/*
 * Copyright (C) 2006 Thomas Leibner (leibner@t-online.de) (first complete writeup)
 *               2002 David Schleef (ds@schleef.org) (COMEDI master)
 *               2002 Lorenzo Dozio (dozio@aero.polimi.it) (made it all work)
 *               2006 Roberto Bucher <roberto.bucher@supsi.ch> (upgrade)
 *               2002-2009 Paolo Mantegazza (mantegazza@aero.polimi.it) (hints/support)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * This file is the kernel module counterpart of the KComedi-LXRT support.
 * It will be compiled to a rtai_comedi.o kernel module.
 */


#include <linux/module.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include <rtai_schedcore.h>
#include <rtai_sched.h>
#include <rtai_lxrt.h>
#include <rtai_shm.h>
#include <rtai_msg.h>
#include <rtai_comedi.h>

#define MODULE_NAME "rtai_comedi.o"
MODULE_DESCRIPTION("RTAI LXRT binding for COMEDI kcomedilib");
MODULE_AUTHOR("The Comedi Players");
MODULE_LICENSE("GPL");

//#define RTAI_COMEDI_USE_LOCK

#ifdef RTAI_COMEDI_USE_LOCK

#define RTAI_COMEDI_LOCK(dev, subdev) \
	do { comedi_lock(dev, subdev); } while (0)
#define RTAI_COMEDI_UNLOCK(dev, subdev) \
	do { comedi_unlock(dev, subdev); } while (0)

#else

#define RTAI_COMEDI_LOCK(dev, subdev)    do { } while (0)
#define RTAI_COMEDI_UNLOCK(dev, subdev)  do { } while (0)

#endif

#define space(adr)  ((unsigned long)adr > PAGE_OFFSET)

static int rtai_comedi_callback(unsigned int, RT_TASK *) __attribute__ ((__unused__));
static int rtai_comedi_callback(unsigned int val, RT_TASK *task)
{
        if (task->magic == RT_TASK_MAGIC) {
		task->resumsg = val;
		rt_task_resume(task);
	}
	return 0;
}

static RTAI_SYSCALL_MODE void *_comedi_open(const char *filename)
{
	return comedi_open(filename);
}

static RTAI_SYSCALL_MODE int _comedi_close(void *dev)
{
	return comedi_close(dev);
}

static RTAI_SYSCALL_MODE int _comedi_lock(void *dev, unsigned int subdev)
{
	return comedi_lock(dev, subdev);
}

static RTAI_SYSCALL_MODE int _comedi_unlock(void *dev, unsigned int subdev)
{
	return comedi_unlock(dev, subdev);
}

static RTAI_SYSCALL_MODE int _comedi_cancel(void *dev, unsigned int subdev)
{
	RTAI_COMEDI_LOCK(dev, subdev);
	return comedi_cancel(dev, subdev);
	RTAI_COMEDI_UNLOCK(dev, subdev);
}

RTAI_SYSCALL_MODE int rt_comedi_register_callback(void *dev, unsigned int subdev, unsigned int mask, int (*callback)(unsigned int, void *), void *task)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_register_callback(dev, subdev, mask, (void *)rtai_comedi_callback, task ? task : rt_whoami());
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_command(void *dev, comedi_cmd *cmd)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, cmd->subdev);
	retval = comedi_command(dev, (void *)cmd);
	RTAI_COMEDI_UNLOCK(dev, cmd->subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_command_test(void *dev, comedi_cmd *cmd)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, cmd->subdev);
	retval = comedi_command_test(dev, (void *)cmd);
	RTAI_COMEDI_UNLOCK(dev, cmd->subdev);
	return retval;
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_read(void *dev, unsigned int subdev, long nchans, lsampl_t *data)
{
	void *aibuf;
	int i, ofsti, ofstf, size;
#if 1
	if (comedi_map(dev, subdev, &aibuf)) {
		return RTE_OBJINV;
	}
	if ((i = comedi_get_buffer_contents(dev, subdev)) < nchans) {
		return i;
	}
	size = comedi_get_buffer_size(dev, subdev);
	RTAI_COMEDI_LOCK(dev, subdev);
	ofstf = ofsti = comedi_get_buffer_offset(dev, subdev);
	for (i = 0; i < nchans; i++) {
		data[i] = *(sampl_t *)(aibuf + ofstf % size);
		ofstf += sizeof(sampl_t);
	}
	comedi_mark_buffer_read(dev, subdev, ofstf - ofsti);
	RTAI_COMEDI_UNLOCK(dev, subdev);
#else
        comedi_device *cdev = (comedi_device *)dev;
	comedi_async *async;

	RTAI_COMEDI_LOCK(dev, subdev);
        if (subdev >= cdev->n_subdevices) {
		return RTE_OBJINV;
        }
        if ((async = (cdev->subdevices + cdev->n_subdevices)->async) == NULL) {
                return RTE_OBJINV;
	}
	aibuf = (sampl_t *)async->prealloc_buf;
	if ((i = comedi_buf_read_n_available(async)) < nchans) {
		return i;
	}
	ofstf = ofsti = async->buf_read_ptr;
	size = async->prealloc_bufsz;
	for (i = 0; i < nchans; i++) {
		data[i] = *(sampl_t *)(aibuf + ofstf % size);
		ofstf += sizeof(sampl_t);
	}
	comedi_buf_read_alloc(async, ofstf - ofsti);
	comedi_buf_read_free(async, ofstf - ofsti);
	RTAI_COMEDI_UNLOCK(dev, subdev);
#endif
	return nchans;
}

#define WAIT       0
#define WAITIF     1
#define WAITUNTIL  2

static inline long _rt_comedi_command_data_wread(void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME until, long *maskarg, int waitmode)
{
	long cbmask, retval, space, mask;
	space = space(maskarg);
	if (space) {
		mask = maskarg[0];
	} else {
		rt_get_user(mask, maskarg);
	}
	switch (waitmode) {
		case WAIT: 
			retval = rt_comedi_wait(&cbmask);
			break;
		case WAITIF: 
			retval = rt_comedi_wait_if(&cbmask);
			break;
		case WAITUNTIL: 
			retval = rt_comedi_wait_until(until, &cbmask);
			break;
		default: // useless, just to avoid compiler warnings
			return RTE_PERM;
	}
	if (!retval && (mask & cbmask)) {
		if (space) {
			maskarg[0] = cbmask;
		} else {
			rt_put_user(cbmask, maskarg);
		}
		return rt_comedi_command_data_read(dev, subdev, nchans, data);
	}
	return retval;
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread(void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *maskarg)
{
	return _rt_comedi_command_data_wread(dev, subdev, nchans, data, (RTIME)0, maskarg, WAIT);
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_if(void *dev, unsigned int subdev, long nchans, lsampl_t *data, long *maskarg)
{
	return _rt_comedi_command_data_wread(dev, subdev, nchans, data, (RTIME)0, maskarg, WAITIF);
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_until(void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME until, long *maskarg)
{
	return _rt_comedi_command_data_wread(dev, subdev, nchans, data, until, maskarg, WAITUNTIL);
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_wread_timed(void *dev, unsigned int subdev, long nchans, lsampl_t *data, RTIME delay, long *cbmask)
{
	return rt_comedi_command_data_wread_until(dev, subdev, nchans, data, rt_get_time() + delay, cbmask);
}

static RTAI_SYSCALL_MODE int _comedi_data_write(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t data)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_data_write(dev, subdev, chan, range, aref, data);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_data_read(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_data_read(dev, subdev, chan, range, aref, data);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_data_read_delayed(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data, unsigned int nanosec)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_data_read_delayed(dev, subdev, chan, range, aref, data, nanosec);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_data_read_hint(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_data_read_hint(dev, subdev, chan, range, aref);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_dio_config(void *dev, unsigned int subdev, unsigned int chan, unsigned int io)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_dio_config(dev, subdev, chan, io);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_dio_read(void *dev, unsigned int subdev, unsigned int chan, unsigned int *val)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_dio_read(dev, subdev, chan, val);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_dio_write(void *dev, unsigned int subdev, unsigned int chan, unsigned int val)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_dio_write(dev, subdev, chan, val);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_dio_bitfield(void *dev, unsigned int subdev, unsigned int mask, unsigned int *bits)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_dio_bitfield(dev, subdev, mask, bits);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_get_n_subdevices(void *dev)
{
	return comedi_get_n_subdevices(dev);
}

static RTAI_SYSCALL_MODE int _comedi_get_version_code(void *dev)
{
	return comedi_get_version_code(dev);
}

RTAI_SYSCALL_MODE char *rt_comedi_get_driver_name(void *dev, char *name)
{
	const char *kname;
	if ((kname = comedi_get_driver_name((void *)dev)) != 0) {
		strncpy(name, kname, COMEDI_NAMELEN);
		return name;
	};
	return NULL;
}

RTAI_SYSCALL_MODE char *rt_comedi_get_board_name(void *dev, char *name)
{
	const char *kname;
	if ((kname = comedi_get_board_name((void *)dev)) != 0) {
		strncpy(name, kname, COMEDI_NAMELEN);
		return name;
	}
	return NULL;
}

static RTAI_SYSCALL_MODE int _comedi_get_subdevice_type(void *dev, unsigned int subdev)
{
	return comedi_get_subdevice_type(dev, subdev);
}

static RTAI_SYSCALL_MODE int _comedi_find_subdevice_by_type(void *dev, int type, unsigned int subd)
{
	return comedi_find_subdevice_by_type(dev, type, subd);
}

static RTAI_SYSCALL_MODE int _comedi_get_n_channels(void *dev, unsigned int subdev)
{
	return comedi_get_n_channels(dev, subdev);
}

static RTAI_SYSCALL_MODE lsampl_t _comedi_get_maxdata(void *dev, unsigned int subdev, unsigned int chan)
{
	return comedi_get_maxdata(dev, subdev, chan);
}

static RTAI_SYSCALL_MODE int _comedi_get_n_ranges(void *dev, unsigned int subdev, unsigned int chan)
{
	return comedi_get_n_ranges(dev, subdev, chan);
}

static RTAI_SYSCALL_MODE int _comedi_do_insn(void *dev, comedi_insn *insn)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, insn->subdev);
	retval = comedi_do_insn(dev, insn);
	RTAI_COMEDI_UNLOCK(dev, insn->subdev);
	return retval;
}

RTAI_SYSCALL_MODE int rt_comedi_do_insnlist(void *dev, comedi_insnlist *ilist)
{
	int i, retval;
	for (i = 0; i < ilist->n_insns; i ++) {
		if ((retval = comedi_do_insn(dev, &ilist->insns[i])) < 0) {
			break;
		}
		ilist->insns[i].n = retval;
	}
	return i;
}

static inline int _rt_comedi_trigger(void *dev, unsigned int subdev)
{
        comedi_insn insn;
        lsampl_t data = 0;
        insn.insn   = INSN_INTTRIG;
        insn.subdev = subdev;
        insn.n      = 1;
        insn.data   = &data;
        return _comedi_do_insn(dev, &insn);
}

RTAI_SYSCALL_MODE int rt_comedi_trigger(void *dev, unsigned int subdev, unsigned int trignum)
{
        return _rt_comedi_trigger(dev, subdev);
}

static RTAI_SYSCALL_MODE int _comedi_poll(void *dev, unsigned int subdev)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_poll(dev, subdev);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

/* DEPRECATED FUNCTION
static RTAI_SYSCALL_MODE int _comedi_get_rangetype(unsigned int minor, unsigned int subdevice, unsigned int chan)
{
	return comedi_get_rangetype(minor, subdevice, chan);
}
*/

static RTAI_SYSCALL_MODE unsigned int _comedi_get_subdevice_flags(comedi_t *dev,unsigned int subdev)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_get_subdevice_flags(dev, subdev);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_get_krange(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, comedi_krange *krange)
{
	return comedi_get_krange(dev, subdev, chan, range, krange);
}

static RTAI_SYSCALL_MODE int _comedi_get_buf_head_pos(void * dev, unsigned int subdev)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_get_buf_head_pos(dev, subdev);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_set_user_int_count(comedi_t *dev, unsigned int subdev, unsigned int buf_user_count)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_set_user_int_count(dev, subdev, buf_user_count);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_map(comedi_t *dev, unsigned int subdev, void *ptr)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_map(dev, subdev, ptr);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static RTAI_SYSCALL_MODE int _comedi_unmap(comedi_t *dev, unsigned int subdev)
{
	int retval;
	RTAI_COMEDI_LOCK(dev, subdev);
	retval = comedi_unmap(dev, subdev);
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return retval;
}

static inline long _rt_comedi_wait(RTIME until, long *cbmask, int waitmode)
{ 
	if (cbmask) {
		long retval;
		RT_TASK *task = _rt_whoami();
		switch (waitmode) {
			case WAIT: 
				retval = rt_task_suspend(task);
				break;
			case WAITIF: 
				retval = rt_task_suspend_if(task);
				break;
			case WAITUNTIL: 
				retval = rt_task_suspend_until(task, until);
				break;
			default: // useless, just to avoid compiler warnings
				return RTE_PERM;
		}
		if (space(cbmask)) {
			cbmask[0] = task->resumsg;
		} else {
			rt_put_user(task->resumsg, cbmask);
		}
		task->resumsg = 0;
		return retval;
	}
	return RTE_PERM;
}

RTAI_SYSCALL_MODE long rt_comedi_wait(long *cbmask)
{ 
	return _rt_comedi_wait((RTIME)0, cbmask, WAIT);
}

RTAI_SYSCALL_MODE long rt_comedi_wait_if(long *cbmask)
{ 
	return _rt_comedi_wait((RTIME)0, cbmask, WAITIF);
}

RTAI_SYSCALL_MODE long rt_comedi_wait_until(RTIME until, long *cbmask)
{
	return _rt_comedi_wait(until, cbmask, WAITUNTIL);
}

RTAI_SYSCALL_MODE long rt_comedi_wait_timed(RTIME delay, long *cbmask)
{
	return rt_comedi_wait_until(rt_get_time() + delay, cbmask);
}

RTAI_SYSCALL_MODE long rt_comedi_command_data_write(void *dev, unsigned int subdev, long nchans, lsampl_t *data)
{
	void *aobuf;
	int i, ofsti, ofstf, size, avbs;
	if (comedi_map(dev, subdev, &aobuf)) {
		return RTE_OBJINV;
	}
	size = comedi_get_buffer_size(dev, subdev);
	avbs = comedi_get_buffer_contents(dev, subdev);
	if ((size - avbs) < nchans) {
		return (size - avbs);
	}
	RTAI_COMEDI_LOCK(dev, subdev);
	ofstf = ofsti = (comedi_get_buffer_offset(dev, subdev) + avbs) % size;
	for (i = 0; i < nchans; i++) {
		*(sampl_t *)(aobuf + ofstf % size) = data[i];
		ofstf += sizeof(sampl_t);
	}
	comedi_mark_buffer_written(dev, subdev, ofstf - ofsti);
	if (!avbs) {
		int retval;
		if ((retval = _rt_comedi_trigger(dev, subdev)) < 0) {
			return retval;
		}
	}
	RTAI_COMEDI_UNLOCK(dev, subdev);
	return nchans;
}

static struct rt_fun_entry rtai_comedi_fun[] = {
  [_KCOMEDI_OPEN]                  = { 0, _comedi_open }
 ,[_KCOMEDI_CLOSE]                 = { 0, _comedi_close }
 ,[_KCOMEDI_LOCK]                  = { 0, _comedi_lock }
 ,[_KCOMEDI_UNLOCK]                = { 0, _comedi_unlock }
 ,[_KCOMEDI_CANCEL]                = { 0, _comedi_cancel }
 ,[_KCOMEDI_REGISTER_CALLBACK]     = { 0, rt_comedi_register_callback }
 ,[_KCOMEDI_COMMAND]               = { 0, _comedi_command }
 ,[_KCOMEDI_COMMAND_TEST]          = { 0, _comedi_command_test }
 ,[_KCOMEDI_TRIGGER    ]           = { 0, rt_comedi_trigger }
 ,[_KCOMEDI_DATA_WRITE]            = { 0, _comedi_data_write}
 ,[_KCOMEDI_DATA_READ]             = { 0, _comedi_data_read }
 ,[_KCOMEDI_DATA_READ_DELAYED]     = { 0, _comedi_data_read_delayed }       
 ,[_KCOMEDI_DATA_READ_HINT]        = { 0, _comedi_data_read_hint }          
 ,[_KCOMEDI_DIO_CONFIG]            = { 0, _comedi_dio_config }
 ,[_KCOMEDI_DIO_READ]              = { 0, _comedi_dio_read }
 ,[_KCOMEDI_DIO_WRITE]             = { 0, _comedi_dio_write }
 ,[_KCOMEDI_DIO_BITFIELD]          = { 0, _comedi_dio_bitfield }
 ,[_KCOMEDI_GET_N_SUBDEVICES]      = { 0, _comedi_get_n_subdevices }
 ,[_KCOMEDI_GET_VERSION_CODE]      = { 0, _comedi_get_version_code }
 ,[_KCOMEDI_GET_DRIVER_NAME]       = { 0, rt_comedi_get_driver_name }
 ,[_KCOMEDI_GET_BOARD_NAME]        = { 0, rt_comedi_get_board_name }
 ,[_KCOMEDI_GET_SUBDEVICE_TYPE]    = { 0, _comedi_get_subdevice_type }
 ,[_KCOMEDI_FIND_SUBDEVICE_TYPE]   = { 0, _comedi_find_subdevice_by_type }
 ,[_KCOMEDI_GET_N_CHANNELS]        = { 0, _comedi_get_n_channels }
 ,[_KCOMEDI_GET_MAXDATA]           = { 0, _comedi_get_maxdata }
 ,[_KCOMEDI_GET_N_RANGES]          = { 0, _comedi_get_n_ranges }
 ,[_KCOMEDI_DO_INSN]               = { 0, _comedi_do_insn }
 ,[_KCOMEDI_DO_INSN_LIST]          = { 0, rt_comedi_do_insnlist }
 ,[_KCOMEDI_POLL]                  = { 0, _comedi_poll }
/*
 ,[_KCOMEDI_GET_RANGETYPE]         = { 0, _comedi_get_rangetype }
*/
 ,[_KCOMEDI_GET_SUBDEVICE_FLAGS]   = { 0, _comedi_get_subdevice_flags }
 ,[_KCOMEDI_GET_KRANGE]            = { 0, _comedi_get_krange }
 ,[_KCOMEDI_GET_BUF_HEAD_POS]      = { 0, _comedi_get_buf_head_pos }
 ,[_KCOMEDI_SET_USER_INT_COUNT]    = { 0, _comedi_set_user_int_count }
 ,[_KCOMEDI_MAP]                   = { 0, _comedi_map }
 ,[_KCOMEDI_UNMAP]                 = { 0, _comedi_unmap }
 ,[_KCOMEDI_WAIT]                  = { 0, rt_comedi_wait }
 ,[_KCOMEDI_WAIT_IF]               = { 0, rt_comedi_wait_if }
 ,[_KCOMEDI_WAIT_UNTIL]            = { 0, rt_comedi_wait_until }
 ,[_KCOMEDI_WAIT_TIMED]            = { 0, rt_comedi_wait_timed }
 ,[_KCOMEDI_COMD_DATA_READ]        = { 0, rt_comedi_command_data_read }
 ,[_KCOMEDI_COMD_DATA_WREAD]       = { 0, rt_comedi_command_data_wread }
 ,[_KCOMEDI_COMD_DATA_WREAD_IF]    = { 0, rt_comedi_command_data_wread_if }
 ,[_KCOMEDI_COMD_DATA_WREAD_UNTIL] = { 0, rt_comedi_command_data_wread_until }
 ,[_KCOMEDI_COMD_DATA_WREAD_TIMED] = { 0, rt_comedi_command_data_wread_timed }
 ,[_KCOMEDI_COMD_DATA_WRITE]       = { 0, rt_comedi_command_data_write }
};

int __rtai_comedi_init(void)
{
	if( set_rt_fun_ext_index(rtai_comedi_fun, FUN_COMEDI_LXRT_INDX) ) {
		printk("Recompile your module with a different index\n");
		return -EACCES;
	}
	return 0;
}

void __rtai_comedi_exit(void)
{
	reset_rt_fun_ext_index(rtai_comedi_fun, FUN_COMEDI_LXRT_INDX);
}

module_init(__rtai_comedi_init);
module_exit(__rtai_comedi_exit);

EXPORT_SYMBOL(rt_comedi_wait);
EXPORT_SYMBOL(rt_comedi_wait_if);
EXPORT_SYMBOL(rt_comedi_wait_until);
EXPORT_SYMBOL(rt_comedi_wait_timed);
EXPORT_SYMBOL(rt_comedi_get_driver_name);
EXPORT_SYMBOL(rt_comedi_get_board_name);
EXPORT_SYMBOL(rt_comedi_register_callback);
EXPORT_SYMBOL(rt_comedi_command_data_read);
EXPORT_SYMBOL(rt_comedi_command_data_wread);
EXPORT_SYMBOL(rt_comedi_command_data_wread_if);
EXPORT_SYMBOL(rt_comedi_command_data_wread_until);
EXPORT_SYMBOL(rt_comedi_command_data_wread_timed);
EXPORT_SYMBOL(rt_comedi_do_insnlist);
EXPORT_SYMBOL(rt_comedi_trigger);
EXPORT_SYMBOL(rt_comedi_command_data_write);
