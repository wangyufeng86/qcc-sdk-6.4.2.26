/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_at_anc.c
\brief      ANC AT handlers for production tests
*/

#ifdef ENABLE_ANC_PRODUCTION_TEST

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

#include "av_headset_at_anc.h"
#include "av_headset_at.h"
#include "av_headset_uart.h"
#include "av_headset_kymera.h"
#include "av_headset_anc_prod_test.h"

#define FFA_PATH (1)
#define FFB_PATH (2)
#define FB_PATH  (3)

#define ANC_INVALID_MODE (0xFF) 
#define MIN_AT_ANC_MODE (anc_mode_1+1)
#define MAX_AT_ANC_MODE (anc_mode_10+1)

#define CHECK_PARAMS()  (param <= end)
#define isblank(c)      (c == ' ')

/*! Convert character to number from the AT string
*/
static bool appAncChToU8(const uint8 ch, uint8 *num)
{
    if (ch >= '0' && ch <= '9')
    {
        *num = ch - '0';
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        *num = ch - 'a' + 10;
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        *num = ch - 'A' + 10;
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

static bool appAncParseNum(const uint8 *start, const uint8 *end,  uint16 *num)
{
    bool status = TRUE;
    uint8 value;

    while (start <= end && isblank(*start)) start++;

    if (start + 2 <= end &&
        start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X'))
    {
        *num = 0;

        for (start += 2; start <= end; start++)
        {
            if (appAncChToU8(*start, &value))
                *num = (*num << 4) | value;
            else
                break;
        }
    }
    else
    {
        const uint8 *sp = start;

        /*Move the start pointer to as many characters as in the number*/
        start = UtilGetNumber(start, end+1, num);

        if (!start)
        {
            start = sp;
            status = FALSE;
        }
    }

    return status;
}


/*! Range check for ANC mode
*/
static bool appAncModeRangeCheck(uint8 mode)
{
    return ((mode>=MIN_AT_ANC_MODE) && (mode<=MAX_AT_ANC_MODE));
}

/*! Map the received ANC mode to the Library mode
*/
static anc_mode_t appAncMapMode(uint8 mode)
{
    anc_mode_t anc_mode=ANC_INVALID_MODE;
    
    switch(mode)
    {
        case 1:
           anc_mode = anc_mode_1;
           break;

        case 2:
           anc_mode = anc_mode_2;
           break;

        case 3:
           anc_mode = anc_mode_3;
           break;

        case 4:
           anc_mode = anc_mode_4;
           break;

        case 5:
           anc_mode = anc_mode_5;
           break;

        case 6:
           anc_mode = anc_mode_6;
           break;

        case 7:
           anc_mode = anc_mode_7;
           break;

        case 8:
           anc_mode = anc_mode_8;
           break;

        case 9:
           anc_mode = anc_mode_9;
           break;

        case 10:
           anc_mode = anc_mode_10;
           break;

        default:
           break;
    }
    
    return anc_mode;
}


static audio_anc_path_id appAncMapGainPath(uint8 gain_path)
{
    audio_anc_path_id anc_gain_path=AUDIO_ANC_PATH_ID_NONE;
    
    switch(gain_path)
    {
        case FFA_PATH:
           anc_gain_path = AUDIO_ANC_PATH_ID_FFA;
           break;
           
        case FFB_PATH:
           anc_gain_path = AUDIO_ANC_PATH_ID_FFB;
           break;
           
        case FB_PATH:
           anc_gain_path = AUDIO_ANC_PATH_ID_FB;
           break;

       default:
        break;
    }
    
    return anc_gain_path;
}


/*! Enable ANC for specified mode in the system for the production tests
      AT+ANCENABLE=<mode>
*/
bool appAncEnable(const uint8 *param, const uint8 *end)
{
    uint16 requested_mode=0;
    bool status = FALSE;

    if ( CHECK_PARAMS() &&
        (appAncParseNum(param, end, &requested_mode)) && 
        (appAncModeRangeCheck(requested_mode)))
    {
        DEBUG_LOG("ANC Enable requested mode %d", requested_mode);

        /*First Enable ANC*/
        if (!appKymeraAncIsEnabled()) 
        {
            DEBUG_LOG("ANC Enabled");
            appKymeraAncEnable();
        }
        else
        {
            DEBUG_LOG("ANC already Enabled");
        }

        /*Set mode works only if ANC is enabled*/
        if (appKymeraAncIsEnabled())
        {
            anc_mode_t mapped_anc_mode = appAncMapMode(requested_mode);
            uint8 gain_path=FFA_PATH;
            uint8 gain=0;

            DEBUG_LOG("Current Mapped Mode %d", mapped_anc_mode);

            AncSetMode(mapped_anc_mode);
            status = TRUE;

            /*This will make sure to return ERROR for invalid mode*/
            if (!AncReadFineGain(appAncMapGainPath(gain_path), &gain))
            {
                status = FALSE;
            }            
        }
    }
    return status;
}

/*! Disable ANC for the production tests
      AT+ANCDISABLE
*/
bool appAncDisable(void)
{
    if (appKymeraAncIsEnabled())
    {
        DEBUG_LOG("ANC DISABLE");
        appKymeraAncDisable();
}

    return TRUE;
}

/*! Reset ANC Calibration mode for the production tests
      AT+ANCRESETCALMODE
*/
bool appAncResetCalMode(void)
{
    DEBUG_LOG("!!!Reset ANC calibration mode!!! ");
    DEBUG_LOG(" !!!ANC calibration mode permanently disabled!!! ");

    appAncProdTestModeDeInit();
    return TRUE;
}


/*! Parse AT command to read ANC fine gain
      AT+ANCREADFINEGAIN=<gain_path>
     +ANCREADFINEGAIN:<gain_value>
     OK or ERROR
*/
bool appAncReadFineGain(const uint8 *param, const uint8 *end)
{
    uint8 gain_path=0;
    bool status = FALSE;

    if (CHECK_PARAMS() && (appAncChToU8(*param, &gain_path)) && (gain_path<=FB_PATH))
    {
        uint8 gain=0;

        /*Reads gain for currently set mode*/
        if (AncReadFineGain(appAncMapGainPath(gain_path), &gain))
        {
            DEBUG_LOG("ANC current gain %d", gain);
            appAtSendResponseWithGain(gain);
            status = TRUE;
        }
    }

    DEBUG_LOG("appAncReadFineGain gain_path=%d", gain_path);
    return status;
}

/*! Parse AT command to set ANC fine gain in DSP for specified gain paths
      AT+ANCSETFINEGAIN=<gain_path>,<gain value>
     OK or ERROR
*/
bool appAncSetFineGain(const uint8 *param, const uint8 *end)
{
    uint8 gain_path=0;
    uint16 gain=0;
    bool status=FALSE;

    if ((CHECK_PARAMS() && (appAncChToU8(*param++, &gain_path)) && (gain_path<=FB_PATH)) && (param++) /*skip comma*/ &&
        (CHECK_PARAMS() && (appAncParseNum(param++, end, &gain)) && (gain<=0xFF)))
    {
        DEBUG_LOG(" appAncSetFineGain gain_path=%d, gain=%d", gain_path, gain);

        if (appKymeraAncIsEnabled())
        {
            /*Set gain for currently set mode in DSP*/
            switch(gain_path)
            {
                case FFA_PATH:
                    status = AncConfigureFFAPathGain(AUDIO_ANC_INSTANCE_0, (uint8)gain);/*For earbud case, only ANC instance 0 is used*/
                    break;
                case FFB_PATH:
                    status = AncConfigureFFBPathGain(AUDIO_ANC_INSTANCE_0, (uint8)gain);/*For earbud case, only ANC instance 0 is used*/
                    break;
                case FB_PATH:
                    status = AncConfigureFBPathGain(AUDIO_ANC_INSTANCE_0, (uint8)gain);/*For earbud case, only ANC instance 0 is used*/
                    break;
                default:
                    break;
            }
        }
    }
    DEBUG_LOG("appAncSetFineGain status %d", status);
    return status;
}

/*! Parse AT command to write ANC fine gain in device Persistence for specified gain paths
      AT +ANCWRITEFINEGAIN=<gain_path>,<gain value>
     OK or ERROR
*/
bool appAncWriteFineGain(const uint8 *param, const uint8 *end)
{
    uint8 gain_path=0;
    uint16 gain=0;

    if ((CHECK_PARAMS() && (appAncChToU8(*param++, &gain_path)) && (gain_path<=FB_PATH)) && (param++) /*skip comma*/&&
        (CHECK_PARAMS() && (appAncParseNum(param++, end, &gain)) && (gain<=0xFF)) )
    {
        DEBUG_LOG("appAtWriteAncGain gain_path=%d, gain=%d ", gain_path, gain);

        /*Writes gain for currently set mode*/
        if (appKymeraAncIsEnabled() && AncWriteFineGain(appAncMapGainPath(gain_path), (uint8)gain))
        {
            DEBUG_LOG("Written Fine Gain to Audio PS key ");
            return TRUE;
        }
    }

    return FALSE;
}



#endif /*ENABLE_ANC_PRODUCTION_TEST*/
