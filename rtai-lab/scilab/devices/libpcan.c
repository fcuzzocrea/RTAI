//****************************************************************************
// Copyright (C) 2001,2002  PEAK System-Technik GmbH
//
// linux@peak-system.com 
// www.peak-system.com
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// Maintainer(s): Klaus Hitschler (klaus.hitschler@gmx.de)
//****************************************************************************

//****************************************************************************
//
// libpcan.c
// the shared library to unify the interface to the devices
// PCAN-ISA, PCAN-Dongle, PCAN-PCI, PCAN-PC104 via their drivers
//
// $Log: libpcan.c,v $
// Revision 1.1  2004/06/06 14:11:52  rpm
// Initial revision
//
// Revision 1.3  2004/02/05 09:36:00  bucher
// *** empty log message ***
//
// Revision 1.1  2003/10/22 12:36:42  bucher
// RTAI 3.0
//
// Revision 1.1  2003/10/03 05:29:55  bucher
// New for RTAI-Lab 2.x
//
// Revision 1.13  2002/08/20 20:06:24  klaus
// Corrected BTR0BTR1 calculation and functions
//
// Revision 1.12  2002/02/21 21:39:47  klaus
// Install problems with RedHat 7.2 removed
//
// Revision 1.11  2002/02/11 18:09:37  klaus
// moved libpcan.h from include/libpcan.h
//
// Revision 1.10  2002/01/30 20:54:27  klaus
// simple source file header change
//
// Revision 1.9  2002/01/13 18:25:56  klaus
// first release
//
// Revision 1.8  2002/01/07 21:17:55  klaus
// smoothed error handling via nGetLastError()
//
//****************************************************************************

//****************************************************************************
// INCLUDES
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libpcan.h>      // the interface to the application

//****************************************************************************
// DEFINES
#define PROCFILE "/proc/pcan"     // where to get information
#define MAX_LINE_LEN 255          // to store a line of text
#define DEVICE_PATH "/dev/pcan"   // + Minor = real device path
#define LOCAL_STRING_LEN 64       // length of internal used strings

//****************************************************************************
// GLOBALS
typedef struct
{
  char szVersionString[LOCAL_STRING_LEN];
  char szDevicePath[LOCAL_STRING_LEN];
  int  nFileNo;
} PCAN_DESCRIPTOR;

//****************************************************************************
// LOCALS

//****************************************************************************
// CODE

//----------------------------------------------------------------------------
// merge a device file path
static char *szDeviceName(int nMinor)
{
  static char path[LOCAL_STRING_LEN];
  
  path[0] = 0;
  
  if (nMinor > 64)
    return path;
    
  sprintf(path, "%s%d", DEVICE_PATH, nMinor);
  
  return path;
} 

static int resolve(char *buffer, int *nType, unsigned long *dwPort, unsigned short *wIrq, int *nMajor, int *nMinor)
{
  static int m_nMajor = 0;
  char *ptr = buffer;
  char *t;
  
  if (*ptr == '\n')
    return -1;
  
  if (*ptr == '*')
  {
    // search for nMajor
    if ((ptr = strstr(ptr, "major"))) 
    {
      t = strtok(ptr, " ");
      t = strtok(NULL, " ");
      m_nMajor = strtoul(t, NULL, 10);
    }
  }
  else
  {
    // skip leading blank
    if (*ptr == ' ')
      ptr++;
      
    // get minor
    t = strtok(ptr, " ");
    *nMinor = strtoul(ptr, NULL, 10);
    
    // get type string
    t = strtok(NULL, " ");
    if (!strcmp(t, "pci"))
      *nType = HW_PCI;
    else
    {
      if (!strcmp(t, "epp"))
        *nType = HW_DONGLE_SJA_EPP;
	    else
	    {
        if (!strcmp(t, "isa"))
          *nType = HW_ISA_SJA;
	      else
	      {
          if (!strcmp(t, "sp"))
            *nType = HW_DONGLE_SJA;
	        else
		        *nType = -1;
	      }
	    }
    }
    
    // get port
    t = strtok(NULL, " ");
    *dwPort = strtoul(t, NULL, 16);
    
    // get irq
    t = strtok(NULL, " ");
    *wIrq   = (unsigned short)strtoul(t, NULL, 10);
    
    // set major
    *nMajor = m_nMajor;
    
    return 0;
  }
  
  return -1;
}

//----------------------------------------------------------------------------
// do a unix like open of the device
HANDLE LINUX_CAN_Open(char *szDeviceName, int nFlag)
{
  PCAN_DESCRIPTOR *desc = NULL;
  
  errno = 0;
  
  if ((desc = (PCAN_DESCRIPTOR *)malloc(sizeof(*desc))) == NULL)
    goto fail;
    
  desc->szVersionString[0] = 0;
  desc->szDevicePath[0]    = 0;
    
  if ((desc->nFileNo = open(szDeviceName, nFlag)) == -1)
    goto fail;
    
  strncpy(desc->szDevicePath, szDeviceName, LOCAL_STRING_LEN);
   
  return (HANDLE)desc;
   
  fail:
  if (desc)
  {
    if (desc->nFileNo > -1)
      close(desc->nFileNo);
    free(desc);
  }
  
  return NULL;
}

//----------------------------------------------------------------------------
// do a peak like open of the device
HANDLE CAN_Open(WORD wHardwareType, ...)
{
  HANDLE handle = NULL;
  FILE *f = NULL;
  char *m = NULL;
  char *p = NULL;
  char *ptr;
  int  found = 0;
  
  va_list args;
  unsigned long  m_dwPort = 0;
  unsigned short m_wIrq   = 0;
  
  int  nMinor = 0;
  int  nMajor = 0;
  unsigned long  dwPort;
  unsigned short wIrq;
  int  nType;
  
  errno = ENODEV;

  // read variable length and type argument list
  va_start(args, wHardwareType);  
  switch(wHardwareType)
  {
    case HW_DONGLE_SJA:
    case HW_DONGLE_SJA_EPP:
    case HW_ISA_SJA:
      m_dwPort = va_arg(args, unsigned long);
      m_wIrq   = (unsigned short)va_arg(args, unsigned long);
      va_end(args);
      break;
      
    case HW_PCI:
      m_dwPort  = va_arg(args, int);
      va_end(args);
      break;
      
    default: 
      va_end(args);
      goto fail;
  }
  
  if ((f = fopen(PROCFILE, "r")) == NULL)
    goto fail;
 
  if ((m = malloc(MAX_LINE_LEN)) == NULL)
  goto fail;
   
  // read an interpret proc entry contents
  do
  {
    ptr = m;
    p = fgets(ptr, MAX_LINE_LEN, f);
    if (p)
    {
      if (!resolve(p, &nType, &dwPort, &wIrq, &nMajor, &nMinor))
      {
        if (wHardwareType == nType)
	      {
          switch (wHardwareType)
	        {
            case HW_DONGLE_SJA:
            case HW_DONGLE_SJA_EPP:
            case HW_ISA_SJA:
              if (((m_dwPort == dwPort) && (m_wIrq == wIrq)) ||
	                ((m_dwPort ==      0) && (m_wIrq ==    0))) // use default
	            {
		            found = 1;
                break;
	            }

            case HW_PCI:
              if (((m_dwPort - 1) == nMinor) || // enumerate 1..8 (not 0 .. 7)
	                 (m_dwPort == 0))             // use 1st port as default
	            {
		            found = 1;
                break;
	            }
	        }
	      }
      }
    }
  } while ((p) && (!found));
  
  if (found)  
    handle = LINUX_CAN_Open(szDeviceName(nMinor), O_RDWR);  
     
  fail:
  if (m)
    free(m);
  if (f)
    fclose(f);
  return handle;
}

//----------------------------------------------------------------------------
// init the CAN chip of this device
DWORD CAN_Init(HANDLE hHandle, WORD wBTR0BTR1, int nCANMsgType)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  
  errno = err;
  if (desc)
  {
    TPCANInit init;
    
    init.wBTR0BTR1    = wBTR0BTR1;    // combined BTR0 and BTR1 register of the SJA100
    init.ucCANMsgType = (nCANMsgType) ? MSGTYPE_EXTENDED : MSGTYPE_STANDARD;  // 11 or 29 bits
    init.ucListenOnly = 0;            // listen only mode when != 0
    
    if ((err = ioctl(desc->nFileNo, PCAN_INIT, &init)) < 0)
      return err;
  }
  
  return err;
}
        
DWORD CAN_Close(HANDLE hHandle)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  
  if (desc)
  {
    if (desc->nFileNo > -1)
    {
      close(desc->nFileNo);
      desc->nFileNo = -1;
    }
    free(desc);
  }
  return 0;
}

DWORD CAN_Status(HANDLE hHandle)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  
  errno = err;  
  if (desc)
  {
    TPSTATUS status;
    
    if ((err = ioctl(desc->nFileNo, PCAN_GET_STATUS, &status)) < 0)
      return err; 
    else
      return status.wErrorFlag;
  }
  
  return err;
}

DWORD CAN_Write(HANDLE hHandle, TPCANMsg* pMsgBuff)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  
  errno = err;  
  if (desc)
  {
    if ((err = ioctl(desc->nFileNo, PCAN_WRITE_MSG, pMsgBuff)) < 0)
      return err;
  }
  
  return err;
}

DWORD CAN_Read(HANDLE hHandle, TPCANMsg* pMsgBuff)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  
  errno = err;  
  if (desc)
  {
    TPCANRdMsg m;
    
    if ((err = ioctl(desc->nFileNo, PCAN_READ_MSG, &m)) < 0)
      return err;
      
    memcpy(pMsgBuff, &m.Msg, sizeof(* pMsgBuff));
  }
  
  return err;
}

DWORD LINUX_CAN_Statistics(HANDLE hHandle, TPDIAG *diag)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  
  errno = err;  
  if (desc)
    err = ioctl(desc->nFileNo, PCAN_DIAG, diag);
  
  return err;
}

WORD LINUX_CAN_BTR0BTR1(HANDLE hHandle, DWORD dwBitRate)
{
  PCAN_DESCRIPTOR *desc = (PCAN_DESCRIPTOR *)hHandle;
  int err = EBADF;
  TPBTR0BTR1 ratix;
  
  ratix.dwBitRate = dwBitRate;
  ratix.wBTR0BTR1 = 0;
  
  errno = err;  
  if (desc)
    err = ioctl(desc->nFileNo, PCAN_BTR0BTR1, &ratix);
    
  if (!err)
  	return ratix.wBTR0BTR1;
	else
		return 0;
}

DWORD CAN_VersionInfo(HANDLE hHandle, LPSTR szTextBuff)
{
  int err;
  TPDIAG diag;
  
  *szTextBuff = 0;
  
  err = (int)LINUX_CAN_Statistics(hHandle, &diag);  
  if (err)
    return err;
    
  strncpy(szTextBuff, diag.szVersionString, VERSIONSTRING_LEN);
  
  return err;
}

int nGetLastError(void)
{
  return errno;
}


// end


