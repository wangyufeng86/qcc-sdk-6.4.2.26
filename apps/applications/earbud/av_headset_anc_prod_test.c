/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_anc_prod_test.c
\brief      Enable production tests for ANC gain calibration
*/

#ifdef ENABLE_ANC_PRODUCTION_TEST

#include <connection.h>
#include <ps.h>
#include <stdlib.h>
#include <stdio.h>
#include <print.h>
#include <logging.h>

#include "av_headset_uart.h"
#include "av_headset.h"
#include "av_headset_anc_prod_test.h"

static bool production_test_mode=FALSE;

/*! Reset production test mode
*/
static void appAncProdResetTestMode(void)
{
    production_test_mode = FALSE;
}

/*! To check if device is in production test mode
*/
bool appAncProdIsTestMode(void)
{
    return production_test_mode;
}

/*! Enter production test mode for ANC gain calibration by default
This mode need to be reset once ANC gain is calibrated on production line by explicitly resetting the securtiy PS key to FALSE (by using AT+ANCRESETCALMODE is using UART/AT mechanism)
*/
void appAncProdTestModeInit(void)
{
    uint16 security_ps_value=0;

    /*Production tests can be triggered only if security_ps_value is TRUE*/
    if (PsRetrieve(PS_SECURITY_KEY_CONFIG , (void*)&security_ps_value, sizeof(security_ps_value)/sizeof(uint16)))
    {
        if (security_ps_value && appUartInitConnection())
        {
            DEBUG_LOG("ANC production test Initialised security_ps_value %d", security_ps_value);
            production_test_mode = TRUE;
        }
    }
}

/*! De init the production test mode for ANC gain calibration
This mode need to be reset once ANC gain is calibrated on production line by explicitly resetting the securtiy PS key to FALSE (by using AT+ANCRESETCALMODE is using UART/AT mechanism)
*/
void appAncProdTestModeDeInit(void)
{
    uint16 security_ps_value=0;

    appAncProdResetTestMode();

    /*Set PS key to FALSE to permanently disable calibration*/
    if (PsRetrieve(PS_SECURITY_KEY_CONFIG , (void*)&security_ps_value, sizeof(security_ps_value)))
    {
        security_ps_value =  (uint16) FALSE;
        PsStore(PS_SECURITY_KEY_CONFIG , (const void*)&security_ps_value, sizeof(security_ps_value));
        DEBUG_LOG("ANC production test security_ps_value reset");
    }
}


#endif /*ENABLE_ANC_PRODUCTION_TEST*/
