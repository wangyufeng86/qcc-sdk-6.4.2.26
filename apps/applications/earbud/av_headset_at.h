/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_at.h
\brief      AT commands parsing for production tests
*/

#ifndef _AV_HEADSET_AT_H_
#define _AV_HEADSET_AT_H_


#ifdef ENABLE_PRODUCTION_AT_COMMANDS

typedef const uint8 *ptr;
#define AT_CRLF_SIZE (2)

/*! Function to Identify end of AT command packet
    Parses the (UART) buffer to identify valid AT commands ending with CRLF(\r\n)
    Incomplete or invalid packets are discarded
    
    \param   start  Pointer to start of buffer
    \param   end    Pointer to end of buffer

    \return   void
 */
ptr appAtEndOfPacket(ptr start, ptr end);


/*! Function to parse the AT command
    AT parsing will only parse the expected ANC AT commands. 
    All other strings will be returned with ERROR
    
    \param   data         Pointer to AT data
    \param   size_data  Size of AT command data including trailing CRLF

    \return   void
 */
void appAtParse(const uint8 *data, uint16 size_data);

/*! Function to print AT command

    \param   start   Pointer to start of buffer
    \param   end    Pointer to end of buffer

    \return   void
 */
void appAtPrintCommand(ptr start, ptr end);

/*! Function to Send AT response with gain params

    \param   gain  ANC gain value

    \return   void
 */
void appAtSendResponseWithGain(uint8 gain);

#else
#define appAtEndOfPacket(start, end) (NULL)
#define appAtParse(data, size) ((void)0)
#define appAtPrintCommand(start, end) ((void)0)
#define appAtSendResponseWithGain(gain) ((void)0)
#endif


#endif /*_AV_HEADSET_AT_H_*/
