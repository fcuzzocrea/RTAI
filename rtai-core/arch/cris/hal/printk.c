/*
COPYRIGHT (C) 1999-2001 David Schleef <ds@schleef.org>
GPL
*/

/*
 *  rt_printk.c, hacked from linux/kernel/printk.c.
 *
 * Modified for RT support, David Schleef.
 *
 * Adapted to RTAI, and restyled his way by Paolo Mantegazza. Now it has been
 * taken away from the fifos module and has become an integral part of the basic
 * RTAI module.
 */
/* perhaps not needed, since module.h and kernel.h are not included. */
#undef MODULE
#define __NO_VERSION__

#include <linux/console.h>
#include <linux/tty.h>
#include <linux/console_struct.h>

#include <rtai.h>

/* Some test programs generate much output.  The buffer length
 * can be increased as necessary, but you should note that since
 * this buffer just scrolls into the Linux buffer, you might
 * lose on the Linux side. */
#define TEMP_BUF_LEN	256

static int buf_front, buf_back;

static char buf[TEMP_BUF_LEN];

#define PRINTK_BUF_LEN	8192

#ifndef CONFIG_ARM
#define PTS_BUFFER_LEN	128

int rt_printk_srq;

static char pts_buffer[PTS_BUFFER_LEN];

/*
 * This function is preferred, because it bypasses much of the
 * console code.  It also doesn't scroll the screen, which is a
 * big bonus for framebuffers.
 *
 * The magic constant has something to do with the size of the
 * screen.  I didn't feel like researching it.
 */
#define LINE_OFFSET 48
#define N_LINES 32
static void print_chars_to_screen(char *s,int len)
{
	int i;
	static int line;

	for(i=0;i<len;i++){
#if 0
		conswitchp->con_putc(vc_cons[fg_console].d,
			((vc_cons[fg_console].d->vc_attr)<<8)|s[i],
			line+LINE_OFFSET,i);
#endif
	}
	line++;
	if(line>=N_LINES)line=0;
}

/*
 * Using the console driver from real-time is not guaranteed
 * to be safe.  Use with caution.
 */
int rtai_print_to_screen(const char *format, ...)
{
        va_list args;
        int len;

        va_start(args, format);
        len = vsprintf(pts_buffer, format, args);
        va_end(args);

	print_chars_to_screen(pts_buffer,len);

	return len;
}
#endif

/* Latency note: We do a rt_spin_lock_irqsave() and then do a bunch
 * of processing.  Is this smart?  No.  The way to get around it
 * is to have multiple buffers, but that has problems as well.
 * Or use rt_mem_mgr.
 *
 * I can think of better ways of doing a rt_printk() spool using
 * a linked list, too.
 */
static char rt_printk_buf[PRINTK_BUF_LEN];
static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;

int rt_printk(const char *fmt, ...)
{
	va_list args;
	int len, i;
	unsigned long flags;

        flags = rt_spin_lock_irqsave(&display_lock);
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
	if (buf_front + len >= PRINTK_BUF_LEN) {
		i = PRINTK_BUF_LEN - buf_front;
		memcpy(rt_printk_buf + buf_front, buf, i);
		memcpy(rt_printk_buf, buf + i, len - i);
		buf_front = len - i;
	} else {
		memcpy(rt_printk_buf + buf_front, buf, len);
		buf_front += len;
	}
        rt_spin_unlock_irqrestore(flags, &display_lock);
	rt_pend_linux_srq(rt_printk_srq);

	return len;
}

void rt_printk_sysreq_handler(void)
{
	int tmp;

	while(1) {
		tmp = buf_front;
		if (buf_back  > tmp) {
			printk("%.*s", PRINTK_BUF_LEN - buf_back, rt_printk_buf + buf_back);
			buf_back = 0;
		}
		if (buf_back == tmp) {
			break;
		}
		printk("%.*s", tmp - buf_back, rt_printk_buf + buf_back);
		buf_back = tmp;
	}
}

void rt_printk_init(void)
{
	rt_printk_srq = rt_request_srq(0, rt_printk_sysreq_handler,
		NULL);
	printk("rt_printk using srq %d\n",rt_printk_srq);
}

void rt_printk_cleanup(void)
{
	rt_free_srq(rt_printk_srq);
}
