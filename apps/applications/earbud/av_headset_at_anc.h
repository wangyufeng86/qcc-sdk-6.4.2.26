/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_at_anc.h
\brief      ANC AT handlers for production tests
*/

#ifndef _AV_HEADSET_AT_ANC_H_
#define _AV_HEADSET_AT_ANC_H_


#ifdef ENABLE_ANC_PRODUCTION_TEST


/*! Enable ANC for specified mode for the production tests
      This mode is for individual configuration (eg. different/dynamic modes under feedforward, feedback or hybrid config)
      The configuration whether the ANC supports feedforward, feedback or hybrid is a build configuration, which is static.
      
    \param   param    Pointer to params in AT command
    \param   end        Pointer to end of AT command data

    \return   bool       TRUE if parsing and key match is success else FALSE
 */
bool appAncEnable(const uint8 *param, const uint8 *end);


/*! Enable ANC for specified mode for the production tests

   \param   param    void

    \return   bool       TRUE if parsing and key match is success else FALSE
 */
bool appAncDisable(void);


/*! Parse AT command to write ANC fine gain in device persistence for specified gain paths

    \param   param    Pointer to params in AT command
    \param   end        Pointer to end of AT command data

    \return   bool       TRUE if parsing and required functionality is success else FALSE
 */
bool appAncWriteFineGain(const uint8 *param, const uint8 *end);


/*! Parse AT command to set ANC fine gain in DSP for specified gain paths

    \param   param    Pointer to params in AT command
    \param   end        Pointer to end of AT command data

    \return   bool       TRUE if parsing and required functionality is success else FALSE
 */
bool appAncSetFineGain(const uint8 *param, const uint8 *end);


/*! Parse AT command to read ANC fine gain from device persistence for specified gain paths

    \param   param    Pointer to params in AT command
    \param   end        Pointer to end of AT command data

    \return   bool       TRUE if parsing and required functionality is success else FALSE
 */
bool appAncReadFineGain(const uint8 *param, const uint8 *end);


/*! Parse AT command to reset the ANC Calibration mode for production tests
      This AT command permanently disables calibration mode.
      !!!This is a mandatory step after calibration. !!!
      
    \param   void

    \return   bool       TRUE if parsing and required functionality is success else FALSE
 */
bool appAncResetCalMode(void);

#else
#define appAncAuth(param, end) (FALSE)
#define appAncEnable(param, end) (FALSE)
#define appAncDisable() (FALSE)
#define appAncWriteFineGain(param, end) (FALSE)
#define appAncSetFineGain(param, end) (FALSE)
#define appAncReadFineGain(param, end) (FALSE)
#define appAncResetCalMode() (FALSE)
#endif

#endif /*_AV_HEADSET_AT_ANC_H_*/
