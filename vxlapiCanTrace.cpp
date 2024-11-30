/*------------------------------------------------------------------------------
| File:
|   xlCANdemo.C
| Project:
|   Sample for XL - Driver Library
|   Example application using 'vxlapi.dll'
|-------------------------------------------------------------------------------
| $Author: visjb $    $Locker: $   $Revision: 101442 $
|-------------------------------------------------------------------------------
| Copyright (c) 2014 by Vector Informatik GmbH.  All rights reserved.
 -----------------------------------------------------------------------------*/

#if defined(_Windows) || defined(_MSC_VER) || defined(__GNUC__)
    #define STRICT
    #include <windows.h>
#endif

#include <stdio.h>

#ifndef UNUSED
static inline void UNUSED2(int dummy, ...)
{
    (void)dummy;
}
    #define UNUSED(...) UNUSED2(0, __VA_ARGS__)
#endif

#define RECEIVE_EVENT_SIZE        1     // DO NOT EDIT! Currently 1 is supported only
#define RX_QUEUE_SIZE             4096  // internal driver queue size in CAN events
#define RX_QUEUE_SIZE_FD          16384 // driver queue size for CAN-FD Rx events
#define ENABLE_CAN_FD_MODE_NO_ISO 0     // switch to activate no iso mode on a CAN FD channel

#include "vxlapi.h"
#include "inttypes.h"

/////////////////////////////////////////////////////////////////////////////
// globals

char           g_AppName[XL_MAX_APPNAME + 1] = "xlCANdemo";           //!< Application name which is displayed in VHWconf
XLportHandle   g_xlPortHandle                = XL_INVALID_PORTHANDLE; //!< Global porthandle (we use only one!)
XLdriverConfig g_xlDrvConfig;                                         //!< Contains the actual hardware configuration
XLaccess       g_xlChannelMask    = 0;                                //!< Global channelmask (includes all founded channels)
XLaccess       g_xlPermissionMask = 0;                                //!< Global permissionmask (includes all founded channels)
unsigned int   g_BaudRate         = 500000;                           //!< Default baudrate
int            g_silent           = 0;                                //!< flag to visualize the message events (on/off)
unsigned int   g_TimerRate        = 0;                                //!< Global timerrate (to toggel)
unsigned int   g_canFdSupport     = 0;                                //!< Global CAN FD support flag
unsigned int   g_canFdModeNoIso   = ENABLE_CAN_FD_MODE_NO_ISO;        //!< Global CAN FD ISO (default) / no ISO mode flag

// tread variables
XLhandle     g_hMsgEvent;      //!< notification handle for the receive queue
HANDLE       g_hRXThread;      //!< thread handle (RX)
HANDLE       g_hTXThread;      //!< thread handle (TX)
int          g_RXThreadRun;    //!< flag to start/stop the RX thread
int          g_TXThreadRun;    //!< flag to start/stop the TX thread (for the transmission burst)
int          g_RXCANThreadRun; //!< flag to start/stop the RX thread
unsigned int g_TXThreadCanId;  //!< CAN-ID the TX thread transmits under
XLaccess     g_TXThreadTxMask; //!< channel mask the TX thread uses for transmitting

////////////////////////////////////////////////////////////////////////////
// functions (Threads)

DWORD WINAPI RxCanFdThread(PVOID par);
DWORD WINAPI RxThread(PVOID par);
DWORD WINAPI TxThread(LPVOID par);

////////////////////////////////////////////////////////////////////////////
// functions (prototypes)
void     demoHelp(void);
void     demoPrintConfig(void);
XLstatus demoCreateRxThread(void);

////////////////////////////////////////////////////////////////////////////

//! demoTransmitRemote

//! transmit a remote frame
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoTransmitRemote(unsigned int txID, XLaccess xlChanMaskTx)
{
    XLstatus     xlStatus;
    unsigned int messageCount = 1;

    if(g_canFdSupport)
    {
        XLcanTxEvent canTxEvt;
        unsigned int cntSent;

        memset(&canTxEvt, 0, sizeof(canTxEvt));

        canTxEvt.tag                     = XL_CAN_EV_TAG_TX_MSG;

        canTxEvt.tagData.canMsg.canId    = txID;
        canTxEvt.tagData.canMsg.msgFlags = XL_CAN_TXMSG_FLAG_RTR;
        canTxEvt.tagData.canMsg.dlc      = 8;

        xlStatus                         = xlCanTransmitEx(g_xlPortHandle, xlChanMaskTx, messageCount, &cntSent, &canTxEvt);
    }
    else
    {
        XLevent xlEvent;

        memset(&xlEvent, 0, sizeof(xlEvent));

        xlEvent.tag               = XL_TRANSMIT_MSG;
        xlEvent.tagData.msg.id    = txID;
        xlEvent.tagData.msg.flags = XL_CAN_MSG_FLAG_REMOTE_FRAME;
        xlEvent.tagData.msg.dlc   = 8;

        xlStatus                  = xlCanTransmit(g_xlPortHandle, xlChanMaskTx, &messageCount, &xlEvent);
    }

    printf("- Transmit REMOTE  : CM(0x%llx), %s\n", g_xlChannelMask, xlGetErrorString(xlStatus));

    return XL_SUCCESS;
}

////////////////////////////////////////////////////////////////////////////

//! demoCreateRxThread

//! set the notification and creates the thread.
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoCreateRxThread(void)
{
    XLstatus xlStatus = XL_ERROR;
    DWORD    ThreadId = 0;

    if(g_xlPortHandle != XL_INVALID_PORTHANDLE)
    {

        // Send a event for each Msg!!!
        xlStatus = xlSetNotification(g_xlPortHandle, &g_hMsgEvent, 1);

        if(g_canFdSupport)
        {
            g_hRXThread = CreateThread(0, 0x1000, RxCanFdThread, (LPVOID)0, 0, &ThreadId);
        }
        else
        {
            g_hRXThread = CreateThread(0, 0x1000, RxThread, (LPVOID)0, 0, &ThreadId);
        }
    }
    return xlStatus;
}

////////////////////////////////////////////////////////////////////////////

//! demoInitDriver

//! initializes the driver with one port and all founded channels which
//! have a connected CAN cab/piggy.
//!
////////////////////////////////////////////////////////////////////////////

XLstatus demoInitDriver(XLaccess *pxlChannelMaskTx, unsigned int *pxlChannelIndex)
{

    XLstatus     xlStatus;
    unsigned int i;
    XLaccess     xlChannelMaskFd      = 0;
    XLaccess     xlChannelMaskFdNoIso = 0;

    // ------------------------------------
    // open the driver
    // ------------------------------------
    xlStatus                          = xlOpenDriver();

    // ------------------------------------
    // get/print the hardware configuration
    // ------------------------------------
    if(XL_SUCCESS == xlStatus)
    {
        xlStatus = xlGetDriverConfig(&g_xlDrvConfig);
    }

    if(XL_SUCCESS == xlStatus)
    {

        // ------------------------------------
        // select the wanted channels
        // ------------------------------------
        g_xlChannelMask = 0;
        for(i = 0; i < g_xlDrvConfig.channelCount; i++)
        {

            // we take all hardware we found and supports CAN
            if(g_xlDrvConfig.channel[i].channelBusCapabilities & XL_BUS_ACTIVE_CAP_CAN)
            {

                if(!*pxlChannelMaskTx)
                {
                    *pxlChannelMaskTx = g_xlDrvConfig.channel[i].channelMask;
                    *pxlChannelIndex  = g_xlDrvConfig.channel[i].channelIndex;
                }

                // check if we can use CAN FD - the virtual CAN driver supports CAN-FD, but we don't use it
                if((g_xlDrvConfig.channel[i].channelCapabilities & XL_CHANNEL_FLAG_CANFD_ISO_SUPPORT) &&
                   (g_xlDrvConfig.channel[i].hwType != XL_HWTYPE_VIRTUAL))
                {
                    xlChannelMaskFd |= g_xlDrvConfig.channel[i].channelMask;

                    // check CAN FD NO ISO support
                    if(g_xlDrvConfig.channel[i].channelCapabilities & XL_CHANNEL_FLAG_CANFD_BOSCH_SUPPORT)
                    {
                        xlChannelMaskFdNoIso |= g_xlDrvConfig.channel[i].channelMask;
                    }
                }
                else
                {
                    g_xlChannelMask |= g_xlDrvConfig.channel[i].channelMask;
                }
            }
        }

        // if we found a CAN FD supported channel - we use it.
        if(xlChannelMaskFd && !g_canFdModeNoIso)
        {
            g_xlChannelMask = xlChannelMaskFd;
            printf("- Use CAN-FD for   : CM=0x%llx\n", g_xlChannelMask);
            g_canFdSupport = 1;
        }

        if(xlChannelMaskFdNoIso && g_canFdModeNoIso)
        {
            g_xlChannelMask = xlChannelMaskFdNoIso;
            printf("- Use CAN-FD NO ISO for   : CM=0x%llx\n", g_xlChannelMask);
            g_canFdSupport = 1;
        }

        if(!g_xlChannelMask)
        {
            printf("ERROR: no available channels found! (e.g. no CANcabs...)\n\n");
            xlStatus = XL_ERROR;
        }
    }

    g_xlPermissionMask = g_xlChannelMask;

    // ------------------------------------
    // open ONE port including all channels
    // ------------------------------------
    if(XL_SUCCESS == xlStatus)
    {

        // check if we can use CAN FD
        if(g_canFdSupport)
        {
            xlStatus = xlOpenPort(&g_xlPortHandle,
                                  g_AppName,
                                  g_xlChannelMask,
                                  &g_xlPermissionMask,
                                  RX_QUEUE_SIZE_FD,
                                  XL_INTERFACE_VERSION_V4,
                                  XL_BUS_TYPE_CAN);
        }
        // if not, we make 'normal' CAN
        else
        {
            xlStatus = xlOpenPort(
                &g_xlPortHandle, g_AppName, g_xlChannelMask, &g_xlPermissionMask, RX_QUEUE_SIZE, XL_INTERFACE_VERSION, XL_BUS_TYPE_CAN);
        }
        printf("- OpenPort         : CM=0x%llx, PH=0x%llx, PM=0x%llx, %s\n",
               g_xlChannelMask,
               (long long int)g_xlPortHandle,
               g_xlPermissionMask,
               xlGetErrorString(xlStatus));
    }

    if((XL_SUCCESS == xlStatus) && (XL_INVALID_PORTHANDLE != g_xlPortHandle))
    {

        // ------------------------------------
        // if we have permission we set the
        // bus parameters (baudrate)
        // ------------------------------------
        if(g_xlChannelMask == g_xlPermissionMask)
        {

            if(g_canFdSupport)
            {
                XLcanFdConf fdParams;

                memset(&fdParams, 0, sizeof(fdParams));

                // arbitration bitrate
                fdParams.arbitrationBitRate = 1000000;
                fdParams.tseg1Abr           = 6;
                fdParams.tseg2Abr           = 3;
                fdParams.sjwAbr             = 2;

                // data bitrate
                fdParams.dataBitRate        = fdParams.arbitrationBitRate * 2;
                fdParams.tseg1Dbr           = 6;
                fdParams.tseg2Dbr           = 3;
                fdParams.sjwDbr             = 2;

                if(g_canFdModeNoIso)
                {
                    fdParams.options = CANFD_CONFOPT_NO_ISO;
                }

                xlStatus = xlCanFdSetConfiguration(g_xlPortHandle, g_xlChannelMask, &fdParams);
                printf("- SetFdConfig.     : ABaudr.=%u, DBaudr.=%u, %s\n",
                       fdParams.arbitrationBitRate,
                       fdParams.dataBitRate,
                       xlGetErrorString(xlStatus));
            }
            else
            {
                xlStatus = xlCanSetChannelBitrate(g_xlPortHandle, g_xlChannelMask, g_BaudRate);
                printf("- SetChannelBitrate: baudr.=%u, %s\n", g_BaudRate, xlGetErrorString(xlStatus));
            }
        }
        else
        {
            printf("-                  : we have NO init access!\n");
        }
    }
    else
    {

        xlClosePort(g_xlPortHandle);
        g_xlPortHandle = XL_INVALID_PORTHANDLE;
        xlStatus       = XL_ERROR;
    }

    return xlStatus;
}

////////////////////////////////////////////////////////////////////////////

//! demoCleanUp()

//! close the port and the driver
//!
////////////////////////////////////////////////////////////////////////////

static XLstatus demoCleanUp(void)
{
    XLstatus xlStatus;

    if(g_xlPortHandle != XL_INVALID_PORTHANDLE)
    {
        xlStatus = xlClosePort(g_xlPortHandle);
        printf("- ClosePort        : PH(0x%llx), %s\n", (long long int)g_xlPortHandle, xlGetErrorString(xlStatus));
    }

    g_xlPortHandle = XL_INVALID_PORTHANDLE;
    xlCloseDriver();

    return XL_SUCCESS; // No error handling
}

////////////////////////////////////////////////////////////////////////////

//! main

//!
//!
////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    XLstatus xlStatus;
    XLaccess xlChanMaskTx  = 0;

    int          stop      = 0;
    int          activated = 0;
    int          c;
    unsigned int xlChanIndex = 0;
    unsigned int txID        = 0x01;
    int          outputMode  = XL_OUTPUT_MODE_NORMAL;
    UNUSED(argc, argv, txID, outputMode);

    printf("----------------------------------------------------------\n");
    printf("- vxlapiCanTrace -\n");
    printf("-             Elektrobit GmbH,  " __DATE__ "              -\n");
#ifdef WIN64
    printf("-             - 64bit Version -                          -\n");
#endif
    printf("----------------------------------------------------------\n");

    // ------------------------------------
    // initialize the driver structures
    // for the application
    // ------------------------------------
    xlStatus = demoInitDriver(&xlChanMaskTx, &xlChanIndex);
    printf("- Init             : %s\n", xlGetErrorString(xlStatus));

    if(XL_SUCCESS == xlStatus)
    {
        // ------------------------------------
        // create the RX thread to read the
        // messages
        // ------------------------------------
        xlStatus = demoCreateRxThread();
        printf("- Create RX thread : %s\n", xlGetErrorString(xlStatus));
    }

    if(XL_SUCCESS == xlStatus)
    {
        // ------------------------------------
        // go with all selected channels on bus
        // ------------------------------------
        xlStatus = xlActivateChannel(g_xlPortHandle, g_xlChannelMask, XL_BUS_TYPE_CAN, XL_ACTIVATE_RESET_CLOCK);
        printf("- ActivateChannel  : CM=0x%llx, %s\n", g_xlChannelMask, xlGetErrorString(xlStatus));
        if(xlStatus == XL_SUCCESS)
            activated = 1;
    }

    // ------------------------------------
    // parse the key - commands
    // ------------------------------------
    while(stop == 0)
    {

        DWORD        n;
        INPUT_RECORD ir;

        ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &ir, 1, &n);

        if((n == 1) && (ir.EventType == KEY_EVENT))
        {

            if(ir.Event.KeyEvent.bKeyDown)
            {

                c = ir.Event.KeyEvent.uChar.AsciiChar;
                switch(c)
                {

                    case 27: // end application
                        stop = 1;
                        break;

                    default:
                        break;
                        // end switch
                }
            }
        }
    } // end while

    if((XL_SUCCESS != xlStatus) && activated)
    {
        xlStatus = xlDeactivateChannel(g_xlPortHandle, g_xlChannelMask);
        printf("- DeactivateChannel: CM(0x%llx), %s\n", g_xlChannelMask, xlGetErrorString(xlStatus));
    }
    demoCleanUp();

    return (0);
} // end main()

///////////////////////////////////////////////////////////////////////////

//! RxThread

//! thread to readout the message queue and parse the incoming messages
//!
////////////////////////////////////////////////////////////////////////////

DWORD WINAPI RxThread(LPVOID par)
{
    XLstatus xlStatus;

    unsigned int msgsrx = RECEIVE_EVENT_SIZE;
    XLevent      xlEvent;
    static bool  init_flag = TRUE;
    UNUSED(par);

    g_RXThreadRun = 1;

    while(g_RXThreadRun)
    {

        WaitForSingleObject(g_hMsgEvent, 10);

        xlStatus = XL_SUCCESS;

        while(!xlStatus)
        {

            msgsrx   = RECEIVE_EVENT_SIZE;

            xlStatus = xlReceive(g_xlPortHandle, &msgsrx, &xlEvent);
            if(xlStatus != XL_ERR_QUEUE_IS_EMPTY)
            {

                if(!g_silent)
                {
                    if(xlEvent.chanIndex == 0)
                    {
                        if(TRUE == init_flag)
                        {
                            printf("+----------------------------------------------------------------------------------+\n");
                            printf("Timestamp        |Channel + DLC   + CAN-ID + D1 + D2 + D3 + D4 + D5 + D6 + D7 + D8 |\n");
                            printf("-----------------|--------+-------+--------+----+----+----+----+----+----+----+----|\n");
                            init_flag = FALSE;
                        }
                        printf("%016lld    %d          %d   %08X  %02X   %02X   %02X   %02X   %02X   %02X   %02X   %02X \n",
                               xlEvent.timeStamp,
                               xlEvent.chanIndex,
                               xlEvent.tagData.msg.dlc,
                               xlEvent.tagData.msg.id,
                               xlEvent.tagData.msg.data[0],
                               xlEvent.tagData.msg.data[1],
                               xlEvent.tagData.msg.data[2],
                               xlEvent.tagData.msg.data[3],
                               xlEvent.tagData.msg.data[4],
                               xlEvent.tagData.msg.data[5],
                               xlEvent.tagData.msg.data[6],
                               xlEvent.tagData.msg.data[7]);
                    }
                }
            }
        }
    }
    return NO_ERROR;
}

///////////////////////////////////////////////////////////////////////////

//! RxCANThread

//! thread to read the message queue and parse the incoming messages
//!
////////////////////////////////////////////////////////////////////////////
DWORD WINAPI RxCanFdThread(LPVOID par)
{
    XLstatus     xlStatus = XL_SUCCESS;
    DWORD        rc;
    XLcanRxEvent xlCanRxEvt;

    UNUSED(par);

    g_RXCANThreadRun = 1;

    while(g_RXCANThreadRun)
    {
        rc = WaitForSingleObject(g_hMsgEvent, 10);
        if(rc != WAIT_OBJECT_0)
            continue;

        do
        {
            xlStatus = xlCanReceive(g_xlPortHandle, &xlCanRxEvt);
            if(xlStatus == XL_ERR_QUEUE_IS_EMPTY)
            {
                break;
            }
            if(!g_silent)
            {
                printf("%s\n", xlCanGetEventString(&xlCanRxEvt));
            }

        } while(XL_SUCCESS == xlStatus);
    }

    return (NO_ERROR);
} // RxCanFdThread

#if 0
//============================================================================
// Name        : VectorVirtualCANTrace.cpp
// Author      : Gero Sparwald
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

    #include <iostream>
using namespace std;

int main() {
	cout << "!!!Hello World!!!" << endl; // prints !!!Hello World!!!
	return 0;
}
#endif
