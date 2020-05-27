/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_at.c
\brief      AT commands parsing for production tests
*/

#ifdef ENABLE_PRODUCTION_AT_COMMANDS

#include <library.h>
#include <connection.h>
#include <panic.h>
#include <ps.h>
#include <pio.h>
#include <stdlib.h>
#include <stdio.h>
#include <stream.h>
#include <string.h>
#include <print.h>
#include <util.h>
#include <anc.h>
#include <logging.h>

#include "av_headset_at.h"
#include "av_headset_uart.h"
#include "av_headset_at_anc.h"


static const char * const anc_common_string        = "AT+ANC";            /*Common string in all AT commands for ANC*/
static const char * const anc_enable_string        = "ENABLE=";           /*Enable ANC in device*/
static const char * const anc_disable_string       = "DISABLE";           /*Disable ANC in device*/
static const char * const anc_read_gain_string     = "READFINEGAIN=";     /*Read fine gain for the enabled mode*/
static const char * const anc_set_gain_string      = "SETFINEGAIN=";      /*Set DSP fine gain for the enabled mode*/
static const char * const anc_write_gain_string    = "WRITEFINEGAIN=";    /*Write to PS Store*/
static const char * const anc_reset_cal_string     = "RESETCALMODE";      /*Permanently disable calibration mode. This is a mandatory step after calibration.*/

static const char * const at_string_res_ok         = "\r\nOK\r\n";
static const char * const at_string_res_error      = "\r\nERROR\r\n";
static const char * const anc_read_gain_string_res = "\r\n+ANCREADFINEGAIN:";


 #define isblank(c)      (c == ' ')
 
 #define AT_SEND_RESPONSE(ret)\
 { if (ret) {\
    DEBUG_LOG("Send Response OK");\
    appAtSendResponse(at_string_res_ok);}\
    else{\
    DEBUG_LOG("Send Response ERROR");\
    appAtSendResponse(at_string_res_error);}\
}

/*! Print received AT command
*/
void appAtPrintCommand(ptr start, ptr end)
{
    ptr c = start;
    
    DEBUG_PRINT("\nReceived AT command\n");    
    while (c != end)
    {
        if (*c == '\r') DEBUG_PRINT("\\r");
        else if (*c == '\n')    DEBUG_PRINT("\\n");
        else   putchar(*c);
        c++;
    }
}

/*! Parse AT command to identify End of Command
The start and end of UART buffer is parsed to identify a valid AT command ending with \r\n
The preceding \r or \n are ignored.
*/
ptr appAtEndOfPacket(ptr start, ptr end)
{
    /*
     Returns
     0   if the buffer holds an incomplete packet
     start+1 if the buffer holds an invalid packet
     end of the first packet otherwise
    */
    if (start == end) 
    {
        return 0;
    }

    if(*start == '\r')
    {
        /* expecting <cr> <lf> ... <cr> <lf> */
        if (end-start >= 4)
        {
            if (start[1] == '\n' && start[2] != '\r')
            {
                ptr cmd = start+2;

                if(*cmd != '\r')
                {
                    cmd = (const uint8*)UtilFind(0xFFFF, '\r', (const uint16*)cmd, 0, 1, (uint16)(end-cmd));

                    return cmd == 0 || cmd + 1 == end ? 0 /* no terminator yet */
                     : cmd[1] == '\n' ? cmd+2 /* valid */
                     : start+1 ; /* invalid terminator */
                }
                else
                {
                    return start+1;
                }
            }
            else
            {
                return start+1;
            }
        }
        else
        {
            /* Can't tell yet */
            return 0;
        }
    }
    else
    {
        /* expecting ... <cr> */
        ptr cmd = start;
        
        while (cmd != end && (*cmd == ' ' || *cmd == '\0' || *cmd == '\t')) 
        {
            ++cmd;
        }
        
        if (cmd != end && (*cmd == '\r' || *cmd == '\n' )) 
        {
            return start+1;
        }
        
        while (cmd != end && *cmd != '\r') 
        {
            ++cmd;
        }
        
        return cmd == end ? 0 : cmd+1;
    }
}


/*! Send standard AT response
*/
static void appAtSendResponse(const char *at_data)
{
    if (at_data)
    {
        appUartSendToSink(at_data);
    }
}

/*! Check if string matched the AT command, return zero if command matches
*/
static int appAtCmdCmp(const uint8 *start, uint16 size, const uint8 **params, const char *cmd)
{
    ptr end = start+strlen(cmd);/*To compare the command*/
    
    /* Skip leading whitespace */
    while (start < end && isblank(*start)) 
    {
        start++;
    }

    if (params) 
    {
        *params = start;
    }
    
    for (; start < end; start++, cmd++)
    {
        if (*start != *cmd)
        {
            /*Ignore whitespace*/
            if (isblank(*start)) 
            {
                break;
            }

            /*Ignore case*/
            if ((*start | 0x20) != (*cmd | 0x20)) 
            {
                return *start - *cmd;
            }
        }
    }

    end = start+size;/*To get the command parameters*/
    if (!*cmd || (*cmd & 0x20))
    {
        if (params)
        {
            while (start < end && isblank(*start)) 
            {
                start++;
            }
            /*Point to the start of command parameters or end of requested command*/
            *params = start;
        }
        return 0;
    }
    return -1;
}


/*! Send AT response with params
*/
void appAtSendResponseWithGain(uint8 gain)
{
    char buf[2];
    sprintf( buf, "%d", gain);

    DEBUG_LOG("appAtSendResponseWithGain");
    appUartSendToSink(anc_read_gain_string_res);
    appUartSendToSink(buf);/*Add the gain parameter*/
    appUartSendToSink("\r\n");
}


/*! Parse AT command to identify ANC messages
*/
void appAtParse(const uint8 *data, uint16 size_data)
{
    const uint8 *params = NULL;
    const uint8 **pparams = &params;
    bool status=FALSE;

    uint16 cmd_size = size_data - AT_CRLF_SIZE;/*Remove the trailing CRLF from the data*/
    const uint8 *data_end = data+cmd_size;
    
    DEBUG_LOG("appAtParse cmd_size %d", cmd_size);

    if (!appAtCmdCmp(data, cmd_size, pparams, anc_common_string))
    {
        uint16 offset = strlen(anc_common_string);
        cmd_size = cmd_size - offset;
        data = data + offset;
        
        if (!appAtCmdCmp(data, cmd_size, pparams, anc_enable_string))
            status=appAncEnable(params, data_end);

        if (!appAtCmdCmp(data, cmd_size, pparams, anc_disable_string))
            status = appAncDisable();
            
        if (!appAtCmdCmp(data, cmd_size, pparams, anc_read_gain_string))
            status = appAncReadFineGain(params, data_end);

        if (!appAtCmdCmp(data, cmd_size, pparams, anc_set_gain_string))
            status = appAncSetFineGain(params, data_end);

        if (!appAtCmdCmp(data, cmd_size, pparams, anc_write_gain_string))
            status = appAncWriteFineGain(params, data_end);

        if (!appAtCmdCmp(data, cmd_size, pparams, anc_reset_cal_string))
            status = appAncResetCalMode();
    }
    
    AT_SEND_RESPONSE(status);
}


#endif /*ENABLE_PRODUCTION_AT_COMMANDS*/
