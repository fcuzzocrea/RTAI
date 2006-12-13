/*
 * Copyright (C) 2006 Thomas Leibner (leibner@t-online.de) (first complete writeup)
 *               2002 David Schleef (ds@schleef.org) (COMEDI master)
 *               2002 Lorenzo Dozio (dozio@aero.polimi.it) (made it all work)
 *               2002 Paolo Mantegazza (mantegazza@aero.polimi.it) (hints/support)
 *               2006 Roberto Bucher <roberto.bucher@supsi.ch> (upgrade)
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

#include <rtai_sched.h>
#include <rtai_lxrt.h>
#include <rtai_shm.h>
#include <rtai_comedi.h>

#define MODULE_NAME "rtai_comedi.o"
MODULE_DESCRIPTION("RTAI LXRT binding for COMEDI kcomedilib");
MODULE_AUTHOR("Thomas Leibner <tl@leibner-it.de>");
MODULE_LICENSE("GPL");

static int rtai_comedi_callback(unsigned int, SEM *) __attribute__ ((__unused__));
static int rtai_comedi_callback(unsigned int val, SEM *sem)
{
  sem->owndby = (void *)val;
  rt_printk("CALLBACK MASK: %u\n", val);
  rt_sem_signal(sem);
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
  return comedi_cancel(dev, subdev);
}

RTAI_SYSCALL_MODE int rt_comedi_register_callback(void *dev, unsigned int subdev, unsigned int mask, SEM *sem)
{
  sem->type = (CNT_SEM & 3) - 2;
  return comedi_register_callback(dev, subdev, mask, (void *)rtai_comedi_callback, sem);
}

struct rt_cmd { comedi_cmd cmd; void *kcmd, *kchanlist, *kdata; unsigned long name; };
static unsigned long namebase = (unsigned long)&namebase;

static RTAI_SYSCALL_MODE int rt_comedi_command(void *dev, struct rt_cmd *cmd)
{
  struct rt_cmd *kcmd;

  get_user(kcmd, &cmd->kcmd);
  kcmd->cmd.chanlist = kcmd->kchanlist;
  kcmd->cmd.data = kcmd->kdata;
  return comedi_command(dev, (void *)kcmd);
}

static RTAI_SYSCALL_MODE int rt_comedi_command_test(void *dev, struct rt_cmd *cmd)
{
  struct rt_cmd *kcmd;

  get_user(kcmd, &cmd->kcmd);
  kcmd->cmd.chanlist = kcmd->kchanlist;
  kcmd->cmd.data = kcmd->kdata;
  return comedi_command_test(dev, (void *)kcmd);
}

static RTAI_SYSCALL_MODE  int _comedi_data_write(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t data)
{
  return comedi_data_write(dev, subdev, chan, range, aref, data);
}

static RTAI_SYSCALL_MODE int _comedi_data_read(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data)
{
  return comedi_data_read(dev, subdev, chan, range, aref, data);
}

static RTAI_SYSCALL_MODE int _comedi_data_read_delayed(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref, lsampl_t *data, unsigned int nanosec)
{
  return comedi_data_read_delayed(dev, subdev, chan, range, aref, data, nanosec);
}

static RTAI_SYSCALL_MODE int _comedi_data_read_hint(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, unsigned int aref)
{
  return comedi_data_read_hint(dev, subdev, chan, range, aref);
}

static RTAI_SYSCALL_MODE int _comedi_dio_config(void *dev, unsigned int subdev, unsigned int chan, unsigned int io)
{
  return comedi_dio_config(dev, subdev, chan, io);
}

static RTAI_SYSCALL_MODE int _comedi_dio_read(void *dev, unsigned int subdev, unsigned int chan, unsigned int *val)
{
  return comedi_dio_read(dev, subdev, chan, val);
}

static RTAI_SYSCALL_MODE int _comedi_dio_write(void *dev, unsigned int subdev, unsigned int chan, unsigned int val)
{
  return comedi_dio_write(dev, subdev, chan, val);
}

static RTAI_SYSCALL_MODE int _comedi_dio_bitfield(void *dev, unsigned int subdev, unsigned int mask, unsigned int *bits)
{
  return comedi_dio_bitfield(dev, subdev, mask, bits);
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
  void *p;
  if ((p = comedi_get_driver_name((void *)dev)) != 0) {
    strncpy(name, p, COMEDI_NAMELEN);
    return name;
  };
  return 0;
}

RTAI_SYSCALL_MODE char *rt_comedi_get_board_name(void *dev, char *name)
{
  void *p;  
  if ((p = comedi_get_board_name((void *)dev)) != 0) {
    strncpy(name, p, COMEDI_NAMELEN);
    return name;
  }
  return 0;
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
  return comedi_do_insn(dev, insn);
}

static RTAI_SYSCALL_MODE int _comedi_poll(void *dev, unsigned int subdev)
{
  return comedi_poll(dev, subdev);
}

/* DEPRECATED FUNCTION
static RTAI_SYSCALL_MODE int _comedi_get_rangetype(unsigned int minor, unsigned int subdevice, unsigned int chan)
{
  return comedi_get_rangetype(minor, subdevice, chan);
}
*/

static RTAI_SYSCALL_MODE unsigned int _comedi_get_subdevice_flags(comedi_t *dev,unsigned int subdevice)
{
  return comedi_get_subdevice_flags(dev, subdevice);
}

static RTAI_SYSCALL_MODE int _comedi_get_krange(void *dev, unsigned int subdev, unsigned int chan, unsigned int range, comedi_krange *krange)
{
  return comedi_get_krange(dev, subdev, chan, range, krange);
}

static RTAI_SYSCALL_MODE int _comedi_get_buf_head_pos(void * dev, unsigned int subdev)
{
  return comedi_get_buf_head_pos(dev,subdev);
}

static RTAI_SYSCALL_MODE int _comedi_set_user_int_count(comedi_t *dev, unsigned int subdevice, unsigned int buf_user_count)
{
  return comedi_set_user_int_count(dev, subdevice, buf_user_count);
}

static RTAI_SYSCALL_MODE int _comedi_map(comedi_t *dev, unsigned int subdev, void *ptr)
{
  return comedi_map(dev, subdev, ptr);
}

static RTAI_SYSCALL_MODE int _comedi_unmap(comedi_t *dev, unsigned int subdev)
{
  return comedi_unmap(dev, subdev);
}

RTAI_SYSCALL_MODE unsigned int rt_comedi_wait(SEM *sem, int *semcnt)
{ 
  int count;
  count = rt_sem_wait(sem); 
  if (semcnt) {
    *semcnt = count;
  }
  return (unsigned int)sem->owndby;
}

RTAI_SYSCALL_MODE unsigned int rt_comedi_wait_if(SEM *sem, int *semcnt)
{ 
  int count;
  count = rt_sem_wait_if(sem); 
  if (semcnt) {
    *semcnt = count;
  }
  return (unsigned int)sem->owndby;
}

RTAI_SYSCALL_MODE unsigned int rt_comedi_wait_until(SEM *sem, RTIME until, int *semcnt)
{
  int count;
  count = rt_sem_wait_until(sem, until); 
  if (semcnt) {
    *semcnt = count;
  }
  return (unsigned int)sem->owndby;
}

RTAI_SYSCALL_MODE unsigned int rt_comedi_wait_timed(SEM *sem, RTIME delay, int *semcnt)
{
  int count;
  count = rt_sem_wait_timed(sem, delay); 
  if (semcnt) {
    *semcnt = count;
  }
  return (unsigned int)sem->owndby;
}

static RTAI_SYSCALL_MODE unsigned long rt_comedi_alloc_cmd(unsigned int chanlist_len, unsigned int data_len, unsigned long *ofst)
{
  struct rt_cmd *kcmd;
  unsigned long name;

  name = namebase++;
  kcmd = rtai_kmalloc(name, sizeof(comedi_cmd) + 3*sizeof(void *) + sizeof(unsigned long) + chanlist_len*sizeof(unsigned int) + data_len*sizeof(sampl_t) + 16);
  kcmd->name = name;
  kcmd->kcmd = kcmd;
  kcmd->kchanlist = &kcmd->name + 1;
  kcmd->kdata = &kcmd->name + chanlist_len + 1;
  ofst[0] = (unsigned long)(kcmd->kchanlist - (unsigned long)kcmd);
  ofst[1] = (unsigned long)(kcmd->kdata - (unsigned long)kcmd);
  return name;
}

static RTAI_SYSCALL_MODE unsigned long rt_comedi_free_cmd(struct rt_cmd *cmd)
{
  struct rt_cmd *kcmd;

  get_user(kcmd, &cmd->kcmd);
  rtai_kfree(kcmd->name);
  return kcmd->name;
}

static struct rt_fun_entry rtai_comedi_fun[] = {
  [_KCOMEDI_OPEN]                = { 0, _comedi_open }
  ,[_KCOMEDI_CLOSE]               = { 0, _comedi_close }
  ,[_KCOMEDI_LOCK]                = { 0, _comedi_lock }
  ,[_KCOMEDI_UNLOCK]              = { 0, _comedi_unlock }
  ,[_KCOMEDI_CANCEL]              = { 0, _comedi_cancel }
  ,[_KCOMEDI_REGISTER_CALLBACK]   = { 0, rt_comedi_register_callback }
  ,[_KCOMEDI_COMMAND]             = { 0, rt_comedi_command }
  ,[_KCOMEDI_COMMAND_TEST]        = { 0, rt_comedi_command_test }
  /* DEPRECATED
     ,[_KCOMEDI_TRIGGER    ]         = { 0, _comedi_trigger }
  */
  ,[_KCOMEDI_DATA_WRITE]          = { 0, _comedi_data_write}
  ,[_KCOMEDI_DATA_READ]           = { 0, _comedi_data_read }
  ,[_KCOMEDI_DATA_READ_DELAYED]   = { 0, _comedi_data_read_delayed }       
  ,[_KCOMEDI_DATA_READ_HINT]      = { 0, _comedi_data_read_hint }          
  ,[_KCOMEDI_DIO_CONFIG]          = { 0, _comedi_dio_config }
  ,[_KCOMEDI_DIO_READ]            = { 0, _comedi_dio_read }
  ,[_KCOMEDI_DIO_WRITE]           = { 0, _comedi_dio_write }
  ,[_KCOMEDI_DIO_BITFIELD]        = { 0, _comedi_dio_bitfield }
  ,[_KCOMEDI_GET_N_SUBDEVICES]    = { 0, _comedi_get_n_subdevices }
  ,[_KCOMEDI_GET_VERSION_CODE]    = { 0, _comedi_get_version_code }
  ,[_KCOMEDI_GET_DRIVER_NAME]     = { 0, rt_comedi_get_driver_name }
  ,[_KCOMEDI_GET_BOARD_NAME]      = { 0, rt_comedi_get_board_name }
  ,[_KCOMEDI_GET_SUBDEVICE_TYPE]  = { 0, _comedi_get_subdevice_type }
  ,[_KCOMEDI_FIND_SUBDEVICE_TYPE] = { 0, _comedi_find_subdevice_by_type }
  ,[_KCOMEDI_GET_N_CHANNELS]      = { 0, _comedi_get_n_channels }
  ,[_KCOMEDI_GET_MAXDATA]         = { 0, _comedi_get_maxdata }
  ,[_KCOMEDI_GET_N_RANGES]        = { 0, _comedi_get_n_ranges }
  ,[_KCOMEDI_DO_INSN]             = { 0, _comedi_do_insn }
  /* NOT YET IMPLEMENTED
     ,[_KCOMEDI_DO_INSN_LIST]        = { 0, _comedi_di_insn_list }
  */
  ,[_KCOMEDI_POLL]                = { 0, _comedi_poll }
  /*

  ,[_KCOMEDI_GET_RANGETYPE]       = { 0, _comedi_get_rangetype }

  */
  ,[_KCOMEDI_GET_SUBDEVICE_FLAGS] = { 0, _comedi_get_subdevice_flags }
  ,[_KCOMEDI_GET_KRANGE]          = { 0, _comedi_get_krange }
  ,[_KCOMEDI_GET_BUF_HEAD_POS]    = { 0, _comedi_get_buf_head_pos }
  ,[_KCOMEDI_SET_USER_INT_COUNT]  = { 0, _comedi_set_user_int_count }
  ,[_KCOMEDI_MAP]                 = { 0, _comedi_map }
  ,[_KCOMEDI_UNMAP]               = { 0, _comedi_unmap }
  ,[_KCOMEDI_WAIT]                = { UW1(2, 3), rt_comedi_wait }
  ,[_KCOMEDI_WAIT_IF]             = { UW1(2, 3), rt_comedi_wait_if }
  ,[_KCOMEDI_WAIT_UNTIL]          = { UW1(4, 5), rt_comedi_wait_until }
  ,[_KCOMEDI_WAIT_TIMED]          = { UW1(4, 5), rt_comedi_wait_timed }
  ,[_KCOMEDI_ALLOC_CMD]		= { 0, rt_comedi_alloc_cmd }
  ,[_KCOMEDI_FREE_CMD]		= { 0, rt_comedi_free_cmd }
};

int __rtai_comedi_init(void)
{
  if( set_rt_fun_ext_index(rtai_comedi_fun, FUN_COMEDI_LXRT_INDX) ) {
    printk("Recompile your module with a different index\n");
    return -EACCES;
  }
  return(0);
}

void __rtai_comedi_exit(void)
{
  reset_rt_fun_ext_index(rtai_comedi_fun, FUN_COMEDI_LXRT_INDX);
}

module_init(__rtai_comedi_init);
module_exit(__rtai_comedi_exit);
