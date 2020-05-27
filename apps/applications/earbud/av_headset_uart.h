/*!
\copyright  Copyright (c) 2019 Qualcomm Technologies International, Ltd.
            All Rights Reserved.
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version   
\file        av_headset_uart.c
\brief      UART transport configuration
*/

#ifndef _AV_HEADSET_UART_H_
#define _AV_HEADSET_UART_H_


#ifdef ENABLE_UART

/*! \brief UI task structure */
typedef struct
{
    /*! The UART task */
    TaskData     task;
    /*! The UART task status */
    bool            initialised;
    /*! The UART Source */
    Source        source;
    /*! The UART Sink */
    Sink            sink;
}uartTaskData;


/*! Initialise Uart Connection

    Function to initialise UART Connection
    This transport is currently used for production tests

    \param         void

    \return         TRUE if Initialisation was success else FALSE
 */
bool appUartInitConnection(void);

/*! Send data to UART Sink

    Function to Send UART data to Sink

    \param at_data   pointer to the AT data

    \return               void
 */
void appUartSendToSink(const char *at_data);

/*! Disconnect UART transport

    Function to disconnect the UART source and sink streams

    \param              void

    \return               void
 */
void appUartDisconnect(void);


#else
#define appUartInitConnection() (FALSE)
#define appUartSendToSink(at_data) ((void)0)
#define appUartDisconnect() ((void)0)

#endif /* ENABLE_UART */


#endif
