/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_uart.c
\brief      UART transport configuration
*/

#ifdef ENABLE_UART

#include <library.h>
#include <connection.h>
#include <panic.h>
#include <ps.h>
#include <pio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stream.h>
#include <string.h>
#include <logging.h>
#include <sink.h>
#include <source.h>
#include <vmal.h>

#include "av_headset_uart.h"
#include "av_headset_at.h"
#include "av_headset_anc_prod_test.h"

static uartTaskData uartData={.task = NULL, .initialised=FALSE, .source=NULL, .sink=NULL};

/*  
 *  These are the PIOs that are assigned to UART for each particular board used.
 *  They need to be changed when necessary; please check the board's circuit
 *  diagram to find the correct PIOs.
 *  
 *  Their default setting is for the Aura Lab board CE787 pins; the user is
 *  responsible for changing these pins for their respective boards.
 */
#define BOARD_UART_TX  0x14
#define BOARD_UART_RX  0x15
#define BOARD_UART_RTS 0x16
#define BOARD_UART_CTS 0x17

/*! Parse UART AT Data
      Check for AT end of packet format and send the packet for further parsing
*/    
static ptr appUartParseAtData(ptr start, ptr end)
{
    ptr cmd;

    for(; (cmd = appAtEndOfPacket(start, end)) != 0; start = cmd)
    {
        if(cmd == start+1)
        {
            /* Packet too short, silently discard one character*/
              continue;
        }
        else
        {
            appAtPrintCommand(start, end);
            appAtParse(start, (uint16) (cmd-start));
        }
    }

    return start;
}
  
/*! Parse UART Source Data
      Check for AT commands
*/    
static void appUartParseSource(Source uartDataIn)
{
    uint16 len = SourceSize(uartDataIn);

    if (len>0)
    {
        ptr src = SourceMap(uartDataIn);
        ptr end = src + SourceSize(uartDataIn);

        appUartParseAtData(src, end);
        SourceDrop(uartDataIn, len);
    }
}


/*! UART Message Handler
       Expecting MESSAGE_MORE_DATA for each AT command
*/    
static void appUartHandler(Task task, MessageId id, Message msg)
{
    UNUSED(task);

    switch (id)
    {
      case MESSAGE_MORE_DATA:
      {
            DEBUG_LOG("appUartHandler: MESSAGE_MORE_DATA");
            if (appAncProdIsTestMode())
            {
                appUartParseSource(((MessageMoreData*)msg)->source);

                /*This must get reset on AT+ANCRESETCALMODE*/
                if (!appAncProdIsTestMode())
                {
                    appUartDisconnect();
                }
            }
            else
            {
                DEBUG_LOG("UART is configured to be used for production mode alone");
                Panic();
            }
      }
      break;

      default:
          /* Indicate we couldn't handle the message */
          DEBUG_LOG("appUartHandler: unhandled 0x%04X", id);
          break;
    }
}

/*! Send AT data to UART Sink
*/    
void appUartSendToSink(const char *cmd)
{
    uint16 cmd_len = strlen(cmd);
    
    if(uartData.sink && (SinkSlack(uartData.sink)) >= cmd_len)
    {
        /* Claim space in the sink, getting the offset to it. */
        uint16 offset = SinkClaim(uartData.sink, cmd_len);

        /* Check we have a valid offset */
        if (offset != 0xFFFF)
        {
            /* Map the sink into memory space. */
            uint8 *snk = SinkMap(uartData.sink);
            
            if(snk == NULL)
            {
                DEBUG_LOG("Invalid Sink pointer");
            }
            (void) PanicNull(snk);

            /* Copy the string into the claimed space. */
            memcpy(snk+offset, cmd, cmd_len);
            
            /* Flush the data out to the UART. */
            PanicZero(SinkFlush(uartData.sink, cmd_len));
        }
        else
        {
            DEBUG_LOG("Invalid sink offset");
        }
    }
    else
    {
        DEBUG_LOG("Invalid UART Sink or Insufficient space in Sink");
    }
}
    
/********************  PUBLIC FUNCTIONS  **************************/


/*! Initialise UART Connection
 */
bool appUartInitConnection(void)
{
    /* Assign the appropriate PIOs to be used by UART. */
    bool status = FALSE;
    uint32 bank = 0;
    uint32 mask = (1<<BOARD_UART_RTS) | (1<<BOARD_UART_CTS) | (1<<BOARD_UART_TX) | (1<<BOARD_UART_RX);

    if (!uartData.initialised)
    {
        DEBUG_LOG("appUartInitConnection start");
        status = PioSetMapPins32Bank(bank, mask, 0);
        PanicNotZero(status);
        
        PioSetFunction(BOARD_UART_RTS, UART_RTS);
        PioSetFunction(BOARD_UART_CTS, UART_CTS);
        PioSetFunction(BOARD_UART_TX, UART_TX);
        PioSetFunction(BOARD_UART_RX, UART_RX);

        StreamUartConfigure(VM_UART_RATE_115K2, VM_UART_STOP_ONE, VM_UART_PARITY_NONE);

        uartData.sink = StreamUartSink();
        PanicNull(uartData.sink);
        /* Configure the sink such that messages are not received */
        SinkConfigure(uartData.sink, VM_SINK_MESSAGES, VM_MESSAGES_NONE);

        uartData.source = StreamUartSource();
        PanicNull(uartData.source);
        /* Configure the source for getting all the messages */
        SourceConfigure(uartData.source, VM_SOURCE_MESSAGES, VM_MESSAGES_ALL);

        /* Associate the task with the stream source */
        uartData.task.handler = appUartHandler;
        MessageStreamTaskFromSink(StreamSinkFromSource(uartData.source), (Task) &uartData.task);
        DEBUG_LOG("appUartInitConnection end");

        uartData.initialised=TRUE;
    }
    else
    {
        DEBUG_LOG("UART Already Initialised");
    }
    
    return uartData.initialised;
}

/*! API to disconnect UART Connection
 */
void appUartDisconnect(void)
{
    if (uartData.sink)
    {
        StreamDisconnect(StreamUartSource(), uartData.sink);
        StreamDisconnect(StreamSourceFromSink(uartData.sink), StreamUartSink());
        StreamConnectDispose(StreamSourceFromSink(uartData.sink));
        
        uartData.sink = NULL;
        uartData.source = NULL;
    }

    uartData.initialised = FALSE;
    uartData.task.handler = NULL;
}

#endif /*ENABLE_UART*/
