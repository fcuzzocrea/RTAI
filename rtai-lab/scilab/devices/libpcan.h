#ifndef __LIBPCAN_H__
#define __LIBPCAN_H__

//****************************************************************************
// Copyright (C) 2001,2002,2003,2004  PEAK System-Technik GmbH
//
// linux@peak-system.com
// www.peak-system.com
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Maintainer(s): Klaus Hitschler (klaus.hitschler@gmx.de)
//****************************************************************************

//****************************************************************************
//
// libpcan.h
// common header to access the functions within pcanlib.so.x.x,
// originally created from Wilhelm Hoppe in pcan_pci.h
//
// $Log: libpcan.h,v $
// Revision 1.2  2004/07/12 08:22:14  bucher
// Updated to pcan-3.5
//
// Revision 1.10  2004/04/13 20:36:33  klaus
// added LINUX_CAN_Read() to get the timestamp. Made libpcan.so.0.1.
//
// Revision 1.8  2004/04/11 22:03:29  klaus
// cosmetic changes
//
// Revision 1.7  2004/04/11 22:03:28  klaus
// cosmetic changes
//
// Revision 1.6  2003/07/26 17:55:18  klaus
// changed from GPL license to LGPL licence
//
// Revision 1.5  2003/03/02 10:58:07  klaus
// merged USB thread into main path
//
// Revision 1.4  2003/03/02 10:58:07  klaus
// merged USB thread into main path
//
// Revision 1.3.2.3  2003/02/05 23:12:19  klaus
// adapted to RedHat 7.2
//
// Revision 1.3.2.2  2003/01/29 21:57:58  klaus
// modified to use with USB
//
// Revision 1.3.2.1  2003/01/29 21:57:58  klaus
// modified to use with USB
//
// Revision 1.3  2002/06/11 18:32:56  klaus
// Added persistence of last init parameters, support for polling operation, support for BTR0BTR1 request, support for kernel 2.4.18
//
// Revision 1.2  2002/02/16 16:38:10  klaus
// cosmetical changes
//
// Revision 1.1  2002/02/11 18:09:04  klaus
// moved from include/libpcan.h
//
//****************************************************************************

//****************************************************************************
// INCLUDES
#include <pcan.h>

//****************************************************************************
// compatibilty defines
#if defined(LPSTR) || defined(HANDLE)
#error "double define for LPSTR, HANDLE found"
#endif

#define LPSTR  char *
#define HANDLE void *

//****************************************************************************
// for CAN_Open(...)

//****************************************************************************
// for CAN_Init(...)

// parameter wBTR0BTR1
// bitrate codes of BTR0/BTR1 registers
#define CAN_BAUD_1M     0x0014  //   1 MBit/s
#define CAN_BAUD_500K   0x001C  // 500 kBit/s
#define CAN_BAUD_250K   0x011C  // 250 kBit/s
#define CAN_BAUD_125K   0x031C  // 125 kBit/s
#define CAN_BAUD_100K   0x432F  // 100 kBit/s
#define CAN_BAUD_50K    0x472F  //  50 kBit/s
#define CAN_BAUD_20K    0x532F  //  20 kBit/s
#define CAN_BAUD_10K    0x672F  //  10 kBit/s
#define CAN_BAUD_5K     0x7F7F  //   5 kBit/s

// parameter nCANMsgType
#define CAN_INIT_TYPE_EX		0x01	//Extended Frame
#define CAN_INIT_TYPE_ST		0x00	//Standart Frame

//****************************************************************************
// error codes are defined in pcan.h
#define CAN_ERR_ANYBUSERR (CAN_ERR_BUSLIGHT | CAN_ERR_BUSHEAVY | CAN_ERR_BUSOFF)

//****************************************************************************
// PROTOTYPES
#ifdef __cplusplus
  extern "C"
{
#endif

//****************************************************************************
//  CAN_Open()
//  creates a path to a CAN port
//
//  for PCAN-Dongle call:             CAN_Open(HW_DONGLE_.., DWORD dwPort, WORD wIrq);
//  for PCAN-ISA or PCAN-PC/104 call: CAN_Open(HW_ISA_SJA, DWORD dwPort, WORD wIrq);
//  for PCAN-PCI call:                CAN_Open(HW_PCI, int nPort); .. enumerate nPort 1..8.
//
//  if ((dwPort == 0) && (wIrq == 0)) CAN_Open() takes the 1st default ISA or DONGLE port.
//  if (nPort == 0) CAN_Open() takes the 1st default PCI port.
//  returns NULL when open failes.
//
//  The first CAN_Open() to a CAN hardware initializes the hardware to default
//  parameter 500 kbit/sec and acceptance of extended frames.
//
HANDLE CAN_Open(WORD wHardwareType, ...);

//****************************************************************************
//  CAN_Init()
//  initializes the CAN hardware with the BTR0 + BTR1 constant "CAN_BAUD_...".
//  nCANMsgType must be filled with "CAN_INIT_TYPE_..".
//  The default initialisation, e.g. CAN_Init is not called,
//  is 500 kbit/sec and extended frames.
//
DWORD CAN_Init(HANDLE hHandle, WORD wBTR0BTR1, int nCANMsgType);

//****************************************************************************
//  CAN_Close()
//  closes the path to the CAN hardware.
//  The last close on the hardware put the chip into passive state.
DWORD CAN_Close(HANDLE hHandle);

//****************************************************************************
//  CAN_Status()
//  request the current (stored) status of the CAN hardware. After the read the
//  stored status is reset.
//  If the status is negative a system error is returned (e.g. -EBADF).
DWORD CAN_Status(HANDLE hHandle);

//****************************************************************************
//  CAN_Write()
//  writes a message to the CAN bus. If the write queue is full the current
//  write blocks until either a message is sent or a error occured.
//
DWORD CAN_Write(HANDLE hHandle, TPCANMsg* pMsgBuff);

//****************************************************************************
//  CAN_Read()
//  reads a message from the CAN bus. If there is no message to read the current
//  request blocks until either a new message arrives or a error occures.
DWORD CAN_Read(HANDLE hHandle, TPCANMsg* pMsgBuff);

//****************************************************************************
//  LINUX_CAN_Read()
//  reads a message WITH TIMESTAMP from the CAN bus. If there is no message 
//  to read the current request blocks until either a new message arrives 
//  or a error occures.
DWORD LINUX_CAN_Read(HANDLE hHandle, TPCANRdMsg* pMsgBuff);

//****************************************************************************
//  CAN_VersionInfo()
//  returns a text string with driver version info.
//
DWORD CAN_VersionInfo(HANDLE hHandle, LPSTR lpszTextBuff);

//****************************************************************************
//  nGetLastError()
//  returns the last stored error (errno of the shared library). The returend
//  error is independend of any path.
//
int nGetLastError(void);

//****************************************************************************
//  LINUX_CAN_Open() - another open, LINUX like
//  creates a path to a CAN port
//
//  input: the path to the device node (e.g. /dev/pcan0)
//  returns NULL when open failes
//
HANDLE LINUX_CAN_Open(char *szDeviceName, int nFlag);

//****************************************************************************
//  LINUX_CAN_Statistics() - get statistics about this devices
//
DWORD LINUX_CAN_Statistics(HANDLE hHandle, TPDIAG *diag);

//****************************************************************************
//  LINUX_CAN_BTR0BTR1() - get the BTR0 and BTR1 from bitrate, LINUX like
//
//  input:  the handle to the device node
//          the bitrate in bits / second, e.g. 500000 bits/sec
//
//  returns 0 if not possible
//          BTR0BTR1 for the interface
//
WORD LINUX_CAN_BTR0BTR1(HANDLE hHandle, DWORD dwBitRate);

#ifdef __cplusplus
}
#endif
#endif // __LIBPCAN_H__
