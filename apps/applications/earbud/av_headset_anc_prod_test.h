/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_anc_prod_test.h
\brief      Enable production tests for ANC gain calibration
*/

#ifndef _AV_HEADSET_ANC_PROD_TEST_H_
#define _AV_HEADSET_ANC_PROD_TEST_H_

#ifdef ENABLE_ANC_PRODUCTION_TEST
/*! API to enter the production test mode

    The function puts the device into production test mode by checking the security PS key
    This mode will be available by default to calibration ANC gain on production line
    This mode will need to be reset once ANC gain is calibrated on production line by explicitly resetting the securtiy PS key to FALSE (by using AT+ANCRESETCALMODE is using UART/AT mechanism)    
    \param         void

    \return         void
 */
void appAncProdTestModeInit(void);


/*! API to check if device is in production test mode
   
    \param         void

    \return         TRUE if production mode is set, else FALSE
 */
bool appAncProdIsTestMode(void);

/*! API to DeInit the production test mode

    This is acheived by explicitly resetting the securtiy PS key to FALSE (by using AT+ANCRESETCALMODE is using UART/AT mechanism)
    This is one time permanent step to disable ANC calibration
    
    \param         void

    \return         void
 */
void appAncProdTestModeDeInit(void);


#else
#define appAncProdTestModeInit() ((void)(0))
#define appProdIsTestMode() (FALSE)
#define appAncProdTestModeDeInit() ((void)0)
#endif


#endif /*_AV_HEADSET_ANC_PROD_TEST_H_*/
