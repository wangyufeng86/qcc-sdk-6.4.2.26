/*!
\copyright  Copyright (c) 2008 - 2018 Qualcomm Technologies International, Ltd.\n
            All Rights Reserved.\n
            Qualcomm Technologies International, Ltd. Confidential and Proprietary.
\version    
\file       av_headset_tasklist.c
\brief      Implementation of a simple list of VM tasks.
*/

#include "av_headset.h"
#include "av_headset_tasklist.h"

#include <panic.h>

/******************************************************************************
 * Internal functions
 ******************************************************************************/
/*! \brief Find the index in the task list array for a given task.

    \param[in]  list        Pointer to a Tasklist.
    \param[in]  search_task Task to search for on list.
    \param[out] index       Index at which search_task is found.

    \return bool TRUE search_task found and index returned.
                 FALSE search_task not found, index not valid.
 */
static bool appTaskListFindTaskIndex(TaskList* list, Task search_task, uint16* index)
{
    for (uint16 iter = 0; iter < list->size_list; iter++)
    {
        if (list->tasks[iter] == search_task)
        {
            *index = iter;
            return TRUE;
        }
    }
    return FALSE;
}

/*! \brief Helper function that iterates through a list but also returns the index. 

    \param[in]      list        Pointer to a Tasklist.
    \param[in,out]  next_task   IN Task from which to continue iterating though list.
                                OUT Next task in the list after next_task passed in.
    \param[out]     index       Index in the list at which OUT next_task was returned.

    \return bool TRUE iteration successful, another next_task and index provided.
                 FALSE empty list or end of list reached. next_task and index not
                       valid.
 */
static bool appTaskListIterateIndex(TaskList* list, Task* next_task, uint16* index)
{
    uint16 tmp_index = 0;

    PanicNull(list);
    PanicNull(next_task);
    PanicNull(index);
    
    /* empty list, we're done */
    if (!list->size_list)
        return FALSE;

    /* next_task == NULL to start at tmp_index 0 */
    if (*next_task == 0)
    {
        *next_task = list->tasks[tmp_index];
        *index = tmp_index;
        return TRUE;
    }
    else
    {
        /* move to next task */
        if (appTaskListFindTaskIndex(list, *next_task, &tmp_index))
        {
            if (tmp_index + 1 < list->size_list)
            {
                *next_task = list->tasks[tmp_index+1];
                *index = tmp_index+1;
                return TRUE;
            }
        }
    }
    /* end of the list */
    *next_task = 0;
    return FALSE;
}

/******************************************************************************
 * External API functions
 ******************************************************************************/
/*! \brief Create a TaskList.
 */
TaskList* appTaskListInit(void)
{
    TaskList* new_list = (TaskList*)PanicUnlessMalloc(sizeof(TaskList));
    if (new_list)
    {
        new_list->size_list = 0;
        new_list->tasks = NULL;
        new_list->data = NULL;
        new_list->data_size = 0;
    }
    return new_list;
}

/*! \brief Create a TaskList that can also store associated data.
 */
TaskList* appTaskListWithDataInit(uint8 data_size)
{
    TaskList* new_list = appTaskListInit();
    if (new_list)
    {
        new_list->data_size = data_size;
    }
    return new_list;
}

/*! \brief Destroy a TaskList.
 */
void appTaskListDestroy(TaskList* list)
{
    PanicNull(list);

    free(list->tasks);
    free(list->data);
    free(list);
}

/*! \brief Determine if a task is on a list.
 */
bool appTaskListIsTaskOnList(TaskList* list, Task search_task)
{
    uint16 tmp;
    PanicNull(list);
    return appTaskListFindTaskIndex(list, search_task, &tmp);
}

/*! \brief Add a task to a list.
 */
bool appTaskListAddTask(TaskList* list, Task add_task)
{
    PanicNull(list);
    PanicNull(add_task);

    /* if already in the list, just return */
    if (appTaskListIsTaskOnList(list, add_task))
        return FALSE;

    /* Resize list */
    list->tasks = realloc(list->tasks, sizeof(Task) * (list->size_list + 1));
    PanicNull(list->tasks);

    /* Add task to list */
    list->tasks[list->size_list] = add_task;
    list->size_list += 1;

    return TRUE;
}

/*! \brief Add a task and data to a list.
 */
bool appTaskListAddTaskWithData(TaskList* list, Task add_task, const void* data)
{
    PanicNull(list);

    if (list->data_size == 0)
        return FALSE;

    if (appTaskListAddTask(list, add_task))
    {
        /* create space in 'data' for new data item, size_list already
         * accounts for the +1 after call to appTaskListAddTask */
        list->data = realloc(list->data, list->data_size * (list->size_list));
        PanicNull(list->data);

        /* but do need to use size_list-1 to access the new last
         * entry in the data array */
        const uint16 offset = (list->size_list - 1) * list->data_size;
        memcpy(&list->data[offset], data, list->data_size);
        return TRUE;
    }

    return FALSE;
}

/*! \brief Remove a task from a list.
 */
bool appTaskListRemoveTask(TaskList* list, Task del_task)
{
    uint16 index = 0;

    PanicNull(list);
    PanicNull(del_task);

    if (appTaskListFindTaskIndex(list, del_task, &index))
    {
        uint16 tasks_to_end = list->size_list - index - 1;
        memmove(&list->tasks[index], &list->tasks[index] + 1, sizeof(Task) * tasks_to_end);

        if (list->data_size)
        {
            const uint16 offset = index * list->data_size;
            memmove(&list->data[offset], &list->data[offset] + list->data_size,
                    list->data_size * tasks_to_end);
        }

        list->size_list -= 1;
        if (list->size_list == 0)
        {
            free(list->tasks);
            list->tasks = NULL;
            free(list->data);
            list->data = NULL;
        }
        else
        {
            /* resize list, if not an empty list now, then check realloc successfully
             * returned the memory */
            list->tasks = realloc(list->tasks, sizeof(Task) * list->size_list);
            PanicNull(list->tasks);
            if (list->data_size)
            {
                list->data = realloc(list->data, list->data_size * list->size_list);
                PanicNull(list->data);
            }
        }

        return TRUE;
    }
    else
        return FALSE;
}

/*! \brief Return number of tasks in list.
 */
uint16 appTaskListSize(TaskList* list)
{
    return list ? list->size_list : 0;
}

/*! \brief Iterate through all tasks in a list.
 */
bool appTaskListIterate(TaskList* list, Task* next_task)
{
    uint16 tmp_index = 0;
    return appTaskListIterateIndex(list, next_task, &tmp_index);
}

/*! \brief Iterate through all tasks in a list returning a copy of data as well.
 */
bool appTaskListIterateWithData(TaskList* list, Task* next_task, void* data)
{
    void *raw_data = NULL;
    if (appTaskListIterateWithDataRaw(list, next_task, &raw_data))
    {
        memcpy(data, raw_data, list->data_size);
        return TRUE;
    }
    return FALSE;
}

/*! \brief Iterate through all tasks in a list returning raw data stored in the
    TaskList.
 */
bool appTaskListIterateWithDataRaw(TaskList* list, Task* next_task, void** raw_data)
{
    uint16 index = 0;

    if (list->data_size == 0)
        return FALSE;

    if (appTaskListIterateIndex(list, next_task, &index))
    {
        const uint16 offset = index * list->data_size;
        *raw_data = &list->data[offset];
        return TRUE;
    }

    return FALSE;
}

/*! \brief Iterate through all tasks in a list calling handler each iteration. */
bool appTaskListIterateWithDataRawFunction(TaskList *list, TaskListIterateWithDataRawHandler handler, void *arg)
{
    Task next_task = 0;
    uint16 index = 0;
    bool proceed = TRUE;

    PanicFalse(list->data_size != 0);

    while (proceed && appTaskListIterateIndex(list, &next_task, &index))
    {
        const uint16 offset = index * list->data_size;
        proceed = handler(next_task, &list->data[offset], arg);
    }
    return proceed;
}

/*! \brief Create a duplicate task list.
 */
TaskList *appTaskListDuplicate(TaskList* list)
{
    TaskList *new_list = NULL;
    
    PanicNull(list);

    if (list->data_size == 0)
        new_list = appTaskListInit();
    else
        new_list = appTaskListWithDataInit(list->data_size);
        
    if (new_list)
    {
        new_list->size_list = list->size_list;
        new_list->tasks = PanicUnlessMalloc(sizeof(Task) * new_list->size_list);
        memcpy(new_list->tasks, list->tasks, sizeof(Task) * new_list->size_list);

        if (new_list->data_size)
        {
            new_list->data = PanicUnlessMalloc(list->data_size * new_list->size_list);
            memcpy(new_list->data, list->data, list->data_size * new_list->size_list);
        }
    }

    return new_list;
}

/*! \brief Send a message (with message body) to all tasks in the task list.
*/
void appTaskListMessageSendWithSize(TaskList *list, MessageId id, void *data, uint16 size_data)
{
    PanicNull(list);

    if (list->size_list)
    {
        int index;
        for (index = 1; index < list->size_list; index++)
        {
            if (size_data)
            {
                void *copy = PanicUnlessMalloc(size_data);
                memcpy(copy, data, size_data);
                MessageSend(list->tasks[index], id, copy);
            }
            else
                MessageSend(list->tasks[index], id, NULL);
        }

        /* Send last message */
        MessageSend(list->tasks[0], id, size_data ? data : NULL);
    }
    else
        MessageFree(id, size_data ? data : NULL);
}

/*! \brief Get a copy of the data stored in the list for a given task.
*/
bool appTaskListGetDataForTask(TaskList* list, Task search_task, void* data)
{
    void *raw_data = NULL;
    if (appTaskListGetDataForTaskRaw(list, search_task, &raw_data))
    {
        memcpy(data, raw_data, list->data_size);
        return TRUE;
    }
    return FALSE;
}

/*! \brief Get the address of the data stored in the list for a given task.
*/
bool appTaskListGetDataForTaskRaw(TaskList* list, Task search_task, void** raw_data)
{
    uint16 index;
    PanicNull(list);
    
    if (list->data_size == 0)
        return FALSE;

    if (appTaskListFindTaskIndex(list, search_task, &index))
    {
        const uint16 offset = index * list->data_size;
        *raw_data = &list->data[offset];
        return TRUE;
    }
    return FALSE;
}

/*! \brief Set the data stored in the list for a given task.
*/
bool appTaskListSetDataForTask(TaskList* list, Task search_task, const void* data)
{
    uint16 index;
    PanicNull(list);

    if (list->data_size == 0)
        return FALSE;

    if (appTaskListFindTaskIndex(list, search_task, &index))
    {
        const uint16 offset = index * list->data_size;
        memcpy(&list->data[offset], data, list->data_size);
        return TRUE;
    }
    return FALSE;
}

/*! \brief Determine if the list is one that supports data.
*/
bool appTaskListIsTaskListWithData(TaskList* list)
{
    PanicNull(list);
    return list->data_size != 0;
}
