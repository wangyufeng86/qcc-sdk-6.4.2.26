/*!
\copyright  Copyright (c) 2018 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       av_headset_temperature.c
\brief      Top level temperature sensing implementation. Uses a temperature sensor
            e.g. thermistor to actually perform the measurements.
*/

#ifdef INCLUDE_TEMPERATURE

#include <panic.h>
#include <pio.h>
#include <vm.h>
#include <hydra_macros.h>
#include <limits.h>

#include "av_headset.h"
#include "av_headset_log.h"
#include "av_headset_temperature.h"
#include "av_headset_temperature_sensor.h"

/*! Messages sent within the temperature module. */
enum headset_temperature_internal_messages
{
    /*! Message sent to trigger a temperature measurement */
    MESSAGE_TEMPERATURE_INTERNAL_MEASUREMENT_TRIGGER = 1,
};


typedef struct
{
    uint8 lower_limit;
    uint8 upper_limit;
    temperatureState state;
} temperatureClientData;

/*! \brief Inform a single client of temperature events */
static bool appTemperatureServiceClient(Task task, void *data_void, void *arg)
{
    temperatureClientData *data = (temperatureClientData *)data_void;
    temperatureTaskData *temperature = arg;
    int8 lower_limit = data->lower_limit;
    int8 upper_limit = data->upper_limit;
    int8 t = temperature->temperature;
    temperatureState next_state;

    next_state = (t >= upper_limit) ? TEMPERATURE_STATE_ABOVE_UPPER_LIMIT :
                    (t <= lower_limit) ? TEMPERATURE_STATE_BELOW_LOWER_LIMIT :
                        TEMPERATURE_STATE_WITHIN_LIMITS;

    if (next_state != data->state)
    {
        MESSAGE_MAKE(ind, TEMPERATURE_STATE_CHANGED_IND_T);
        ind->state = next_state;
        data->state = next_state;
        MessageSend(task, TEMPERATURE_STATE_CHANGED_IND, ind);
    }

    /* Iterate through every client */
    return TRUE;
}

/*! \brief Inform all clients of temperature events */
static void appTemperatureServiceClients(temperatureTaskData *temperature)
{
    appTaskListIterateWithDataRawFunction(temperature->clients, appTemperatureServiceClient, temperature);
}

/*! \brief Handle temperature messages */
static void appTemperatureHandleMessage(Task task, MessageId id, Message message)
{
    temperatureTaskData *temperature = (temperatureTaskData *)task;
    switch (id)
    {
        case MESSAGE_TEMPERATURE_INTERNAL_MEASUREMENT_TRIGGER:
            appTemperatureSensorRequestMeasurement(task);
        break;

        default:
        {
            int8 t_new = temperature->temperature;
            if (appTemperatureSensorHandleMessage(task, id, message, &t_new))
            {
                temperature->lock = 0;
                temperature->temperature = t_new;
                appTemperatureServiceClients(temperature);
                MessageSendLater(&temperature->task,
                                 MESSAGE_TEMPERATURE_INTERNAL_MEASUREMENT_TRIGGER, NULL,
                                 appConfigTemperatureMeasurementIntervalMs());
            }
        }
        break;
    }
}


void appTemperatureInit(void)
{
    temperatureTaskData *temperature = appGetTemperature();

    DEBUG_LOG("appTemperatureInit");

    temperature->clients = appTaskListWithDataInit(sizeof(temperatureClientData));
    temperature->task.handler = appTemperatureHandleMessage;
    temperature->temperature = SCHAR_MIN;

    appTemperatureSensorInit();
    appTemperatureSensorRequestMeasurement(&temperature->task);

    temperature->lock = 1;
    MessageSendConditionally(appGetAppTask(), TEMPERATURE_INIT_CFM, NULL, &temperature->lock);
}

void appTemperatureDestroy(void)
{
    temperatureTaskData *temperature = appGetTemperature();
    DEBUG_LOG("appTemperatureDestroy");

    appTaskListDestroy(temperature->clients);
    memset(temperature, 0, sizeof(*temperature));
}

bool appTemperatureClientRegister(Task task, int8 lower_limit, int8 upper_limit)
{
    temperatureTaskData *temperature = appGetTemperature();
    temperatureClientData data;

    DEBUG_LOGF("appTemperatureClientRegister Task=%p (%d, %d)", task, lower_limit, upper_limit);

    data.lower_limit = lower_limit;
    data.upper_limit = upper_limit;
    data.state = TEMPERATURE_STATE_UNKNOWN;
    appTemperatureServiceClient(task, &data, temperature);
    PanicFalse(appTaskListAddTaskWithData(temperature->clients, task, &data));
    return TRUE;
}

void appTemperatureClientUnregister(Task task)
{
    temperatureTaskData *temperature = appGetTemperature();

    DEBUG_LOGF("appTemperatureClientUnregister Task=%p", task);

    PanicFalse(appTaskListRemoveTask(temperature->clients, task));
}

temperatureState appTemperatureClientGetState(Task task)
{
    temperatureTaskData *temperature = appGetTemperature();
    temperatureClientData *data;
    PanicFalse(appTaskListGetDataForTaskRaw(temperature->clients, task, (void **)&data));
    return (temperatureState)data->state;
}

int8 appTemperatureGet(void)
{
    return appGetTemperature()->temperature;
}

#endif
