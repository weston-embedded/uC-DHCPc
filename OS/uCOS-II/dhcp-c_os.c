/*
*********************************************************************************************************
*                                              uC/DHCPc
*                             Dynamic Host Configuration Protocol Client
*
*                    Copyright 2004-2020 Silicon Laboratories Inc. www.silabs.com
*
*                                 SPDX-License-Identifier: APACHE-2.0
*
*               This software is subject to an open source license and is distributed by
*                Silicon Laboratories Inc. pursuant to the terms of the Apache License,
*                    Version 2.0 available at www.apache.org/licenses/LICENSE-2.0.
*
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*
*                                 DHCP CLIENT OPERATING SYSTEM LAYER
*
*                                          Micrium uC/OS-II
*
* Filename : dhcp-c_os.c
* Version  : V2.11.00
*********************************************************************************************************
* Note(s)  : (1) Assumes uC/OS-II V2.86 (or more recent version) is included in the project build.
*
*            (2) REQUIREs the following uC/OS-II feature(s) to be ENABLED :
*
*                    ------ FEATURE ------     -- MINIMUM CONFIGURATION FOR DHCPc/OS PORT --
*
*                (a) Semaphores
*                    (1) OS_SEM_EN             Enabled
*                    (2) OS_SEM_SET_EN         Enabled
*
*                (b) Timers
*                    (1) OS_TMR_EN             Enabled
*
*                (c) Message Queues
*                    (1) OS_Q_EN               Enabled
*
*                (d) OS Events                 OS_MAX_EVENTS >= DHCPc_OS_NBR_EVENTS
*                                                  (see 'OS OBJECT DEFINES')
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            INCLUDE FILES
*********************************************************************************************************
*/

#define    MICRIUM_SOURCE
#include  "../../Source/dhcp-c.h"
#include  <Source/ucos_ii.h>                                           /* See this 'dhcp-c_os.c  Note #1'.                     */


/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                     OS TASK/OBJECT NAME DEFINES
*********************************************************************************************************
*/

                                                                /* -------------------- TASK NAMES -------------------- */
                                          /*           1         2 */
                                          /* 012345678901234567890 */
#define  DHCPc_OS_TASK_NAME                 "DHCPc Task"
#define  DHCPc_OS_TMR_TASK_NAME             "DHCPc Tmr Task"

#define  DHCPc_OS_TASK_NAME_SIZE_MAX                      15    /* Max of ALL DHCPc task name sizes.                    */


                                                                /* -------------------- OBJ NAMES --------------------- */
                                          /*           1         2 */
                                          /* 012345678901234567890 */
#define  DHCPc_OS_INIT_NAME                 "DHCPc Init Signal"
#define  DHCPc_OS_LOCK_NAME                 "DHCPc Global Lock"
#define  DHCPc_OS_TMR_NAME                  "DHCPc Tmr"
#define  DHCPc_OS_TMR_SIGNAL_NAME           "DHCPc Tmr Signal"
#define  DHCPc_OS_Q_NAME                    "DHCPc Msg Q"

#define  DHCPc_OBJ_NAME_SIZE_MAX                          18    /* Max of ALL DHCPc obj name sizes.                     */


/*
*********************************************************************************************************
*                                          OS OBJECT DEFINES
*********************************************************************************************************
*/

#define  DHCPc_OS_NBR_SEM_DHCPc_INIT                       1
#define  DHCPc_OS_NBR_SEM_DHCPc_LOCK                       1
#define  DHCPc_OS_NBR_SEM_DHCPc_TMR_SIGNAL                 1

#define  DHCPc_OS_NBR_SEM                              (DHCPc_OS_NBR_SEM_DHCPc_INIT + \
                                                        DHCPc_OS_NBR_SEM_DHCPc_LOCK + \
                                                        DHCPc_OS_NBR_SEM_DHCPc_TMR_SIGNAL)

#define  DHCPc_OS_NBR_Q                                    1

#define  DHCPc_OS_NBR_EVENTS                           (DHCPc_OS_NBR_SEM + \
                                                        DHCPc_OS_NBR_Q)

                                                                /* 1 msg per IF.                                        */
#define  DHCPc_OS_SIZE_Q                               (DHCPc_CFG_MAX_NBR_IF * DHCPc_COMM_MSG_MAX_NBR)


/*
*********************************************************************************************************
*                                          OS TIMER DEFINES
*********************************************************************************************************
*/

                                                                /* Period of DHCPc tmr in tmr tick.                     */
#define  DHCPc_OS_TMR_PERIOD_TMR_TICK                  (DHCPc_TMR_PERIOD_SEC * OS_TMR_CFG_TICKS_PER_SEC)


/*
*********************************************************************************************************
*                                           LOCAL CONSTANTS
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                          LOCAL DATA TYPES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                            LOCAL TABLES
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                       LOCAL GLOBAL VARIABLES
*********************************************************************************************************
*/

                                                                /* --------------------- TASK STK --------------------- */
static  OS_STK     DHCPc_OS_TaskStk[DHCPc_OS_CFG_TASK_STK_SIZE];
static  OS_STK     DHCPc_OS_TmrTaskStk[DHCPc_OS_CFG_TMR_TASK_STK_SIZE];


                                                                /* ----------------- LOCKS & SIGNALS ------------------ */
static  OS_EVENT  *DHCPc_OS_InitSignalPtr;
static  OS_EVENT  *DHCPc_OS_LockPtr;
static  OS_EVENT  *DHCPc_OS_TmrSignalPtr;


                                                                /* ---------------------- TIMER ----------------------- */
static  OS_TMR    *DHCPc_OS_TmrPtr;


                                                                /* ------------------ MESSAGE QUEUE ------------------- */
static  OS_EVENT  *DHCPc_OS_MsgQPtr;
static  void      *DHCPc_OS_MsgQ[DHCPc_OS_SIZE_Q];


/*
*********************************************************************************************************
*                                      LOCAL FUNCTION PROTOTYPES
*********************************************************************************************************
*/

                                                                /* ---------- DHCPc TASK MANAGEMENT FUNCTION ---------- */
static  void  DHCPc_OS_Task       (void  *p_data);

                                                                /* ------- DHCPc TIMER TASK MANAGEMENT FUNCTION ------- */
static  void  DHCPc_OS_TmrTask    (void  *p_data);

                                                                /* ---------- DHCPc TIMER CALLBACK FUNCTION ----------- */
static  void  DHCPc_OS_TmrCallback(void  *p_tmr,
                                   void  *p_arg);


/*
*********************************************************************************************************
*                                        DEFAULT CONFIGURATION
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                     LOCAL CONFIGURATION ERRORS
*********************************************************************************************************
*/

                                                                /* See this 'dhcp-c_os.c  Note #1'.                     */
#if     (OS_VERSION < 286u)
#error  "OS_VERSION                      [SHOULD be >= V2.86]                "
#endif



                                                                /* See this 'dhcp-c_os.c  Note #2a'.                    */
#if     (OS_SEM_EN < 1u)
#error  "OS_SEM_EN                       illegally #define'd in 'os_cfg.h'             "
#error  "                                [MUST be  > 0, (see 'dhcp-c_os.c  Note #2a1')]"
#endif

#if     (OS_SEM_SET_EN < 1u)
#error  "OS_SEM_SET_EN                   illegally #define'd in 'os_cfg.h'             "
#error  "                                [MUST be  > 0, (see 'dhcp-c_os.c  Note #2a2')]"
#endif



                                                                /* See this 'dhcp-c_os.c  Note #2b'.                    */
#if     (OS_TMR_EN < 1u)
#error  "OS_TMR_EN                       illegally #define'd in 'os_cfg.h'             "
#error  "                                [MUST be  > 0, (see 'dhcp-c_os.c  Note #2b1')]"
#endif



                                                                /* See this 'dhcp-c_os.c  Note #2c'.                    */
#if     (OS_Q_EN < 1u)
#error  "OS_Q_EN                         illegally #define'd in 'os_cfg.h'             "
#error  "                                [MUST be  > 0, (see 'dhcp-c_os.c  Note #2c1')]"
#endif



                                                                /* See this 'dhcp-c_os.c  Note #2d'.                    */
#if     (OS_MAX_EVENTS < DHCPc_OS_NBR_EVENTS)
#error  "OS_MAX_EVENTS                   illegally #define'd in 'os_cfg.h'    "
#error  "                                [MUST be  >= DHCPc_OS_NBR_EVENTS    ]"
#error  "                                [      (see 'dhcp-c_os.c  Note #2d')]"
#endif




#ifndef  DHCPc_OS_CFG_TASK_PRIO
#error  "DHCPc_OS_CFG_TASK_PRIO                not #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  >= 0]                      "
#elif   (DHCPc_OS_CFG_TASK_PRIO < 0u)
#error  "DHCPc_OS_CFG_TASK_PRIO          illegally #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  >= 0]                      "
#endif

#ifndef  DHCPc_OS_CFG_TMR_TASK_PRIO
#error  "DHCPc_OS_CFG_TMR_TASK_PRIO            not #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  >= 0]                      "
#elif   (DHCPc_OS_CFG_TMR_TASK_PRIO < 0u)
#error  "DHCPc_OS_CFG_TMR_TASK_PRIO      illegally #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  >= 0]                      "
#endif



#ifndef  DHCPc_OS_CFG_TASK_STK_SIZE
#error  "DHCPc_OS_CFG_TASK_STK_SIZE            not #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  > 0]                    "
#elif   (DHCPc_OS_CFG_TASK_STK_SIZE < 1u)
#error  "DHCPc_OS_CFG_TASK_STK_SIZE      illegally #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  > 0]                       "
#endif

#ifndef  DHCPc_OS_CFG_TMR_TASK_STK_SIZE
#error  "DHCPc_OS_CFG_TMR_TASK_STK_SIZE        not #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  > 0]                       "
#elif   (DHCPc_OS_CFG_TMR_TASK_STK_SIZE < 1u)
#error  "DHCPc_OS_CFG_TMR_TASK_STK_SIZE  illegally #define'd in 'dhcp-c_cfg.h'"
#error  "                                [MUST be  > 0]                       "
#endif


/*
*********************************************************************************************************
*********************************************************************************************************
*                                   DHCPc INITIALIZATION FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                           DHCPc_OS_Init()
*
* Description : (1) Perform DHCPc/OS initialization :
*
*                   (a) Implement DHCPc initialization signal by creating a counting semaphore.
*
*                       (1) Initialize DHCPc initialization signal with no signal by setting the
*                           semaphore count to 0 to block the semaphore.
*
*                   (b) Implement global DHCPc lock by creating a binary semaphore.
*
*                       (1) Initialize DHCPc lock as released by setting the semaphore count to 1.
*
*                   (c) Implement timer expiration communication by creating a message queue.
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               DHCPc/OS initialization successful.
*
*                               DHCPc_OS_ERR_INIT_SIGNAL        DHCPc    initialization signal
*                                                                   NOT successfully initialized.
*                               DHCPc_OS_ERR_INIT_SIGNAL_NAME   DHCPc    initialization signal name
*                                                                   NOT successfully configured.
*
*                               DHCPc_OS_ERR_INIT_LOCK          DHCPc    lock           signal
*                                                                   NOT successfully initialized.
*                               DHCPc_OS_ERR_INIT_LOCK_NAME     DHCPc    lock           signal name
*                                                                   NOT successfully configured.
*
*                               DHCPc_OS_ERR_INIT_Q             DHCPc    queue
*                                                                   NOT successfully configured.
*                               DHCPc_OS_ERR_INIT_Q_NAME        DHCPc    queue                 name
*                                                                   NOT successfully configured.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : none.
*********************************************************************************************************
*/
void  DHCPc_OS_Init (DHCPc_ERR  *perr)
{
#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OBJ_NAME_SIZE_MAX)))
    INT8U  os_err;
#endif

                                                                /* -------------- INITIALIZE DHCPc SIGNAL ------------- */
                                                                /* Create DHCPc initialization signal ...               */
    DHCPc_OS_InitSignalPtr = OSSemCreate((INT16U)0);            /* ... with NO DHCPc tasks signaled (see Note #1a1).    */
    if (DHCPc_OS_InitSignalPtr == (OS_EVENT *)0) {
       *perr = DHCPc_OS_ERR_INIT_SIGNAL;
        return;
    }

#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OBJ_NAME_SIZE_MAX)))
    OSEventNameSet((OS_EVENT *) DHCPc_OS_InitSignalPtr,
                   (INT8U    *) DHCPc_OS_INIT_NAME,
                   (INT8U    *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_SIGNAL_NAME;
        return;
    }
#endif

                                                                /* --------------- INITIALIZE DHCPc LOCK -------------- */
                                                                /* Create DHCPc lock signal ...                         */
    DHCPc_OS_LockPtr = OSSemCreate((INT16U)1);                  /* ... with DHCPc access available (see Note #1b1).     */
    if (DHCPc_OS_LockPtr == (OS_EVENT *)0) {
       *perr = DHCPc_OS_ERR_INIT_LOCK;
        return;
    }

#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OBJ_NAME_SIZE_MAX)))
    OSEventNameSet((OS_EVENT *) DHCPc_OS_LockPtr,
                   (INT8U    *) DHCPc_OS_LOCK_NAME,
                   (INT8U    *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_LOCK_NAME;
        return;
    }
#endif


                                                                /* ---------- INITIALIZE DHCPc MESSAGE QUEUE ---------- */
    DHCPc_OS_MsgQPtr = OSQCreate(&DHCPc_OS_MsgQ[0],             /* Create DHCPc message queue (see Note #1c).           */
                                  DHCPc_OS_SIZE_Q);
    if (DHCPc_OS_MsgQPtr == (OS_EVENT *)0) {
       *perr = DHCPc_OS_ERR_INIT_Q;
        return;
    }

#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OBJ_NAME_SIZE_MAX)))
    OSEventNameSet((OS_EVENT *) DHCPc_OS_MsgQPtr,
                   (INT8U    *) DHCPc_OS_Q_NAME,
                   (INT8U    *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_Q_NAME;
        return;
    }
#endif


   *perr = DHCPc_OS_ERR_NONE;
}


/*
*********************************************************************************************************
*                                         DHCPc_OS_InitWait()
*
* Description : Wait on signal indicating DHCPc initialization is complete.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               Initialization signal     received.
*                               DHCPc_OS_ERR_INIT               Initialization signal NOT received.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TaskHandler(),
*               DHCPc_TmrTaskHandler().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc initialization signal MUST be acquired--i.e. MUST wait for access; do NOT timeout.
*
*                   (a) Failure to acquire signal will prevent DHCPc task(s) from running.
*********************************************************************************************************
*/

void  DHCPc_OS_InitWait (DHCPc_ERR  *perr)
{
    INT8U  os_err;


    OSSemPend((OS_EVENT *) DHCPc_OS_InitSignalPtr,              /* Wait until DHCPc initialization completes ...        */
              (INT16U    ) 0,                                   /* ... without timeout (see Note #1).                   */
              (INT8U    *)&os_err);

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_PEVENT_NULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_PEND_ISR:
        case OS_ERR_PEND_LOCKED:
        case OS_ERR_PEND_ABORT:
        case OS_ERR_TIMEOUT:
        default:
            *perr = DHCPc_OS_ERR_INIT;                          /* See Note #1a.                                        */
             break;
    }
}


/*
*********************************************************************************************************
*                                        DHCPc_OS_InitSignal()
*
* Description : Signal that DHCPc initialization is complete.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               DHCPc initialization     successfully signaled.
*                               DHCPc_OS_ERR_INIT_SIGNALD       DHCPc initialization NOT successfully signaled.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc initialization MUST be signaled--i.e. MUST signal without failure.
*
*                   (a) Failure to signal will prevent DHCPc task(s) from running.
*********************************************************************************************************
*/

void  DHCPc_OS_InitSignal (DHCPc_ERR  *perr)
{
    INT8U  os_err;


    os_err = OSSemPost(DHCPc_OS_InitSignalPtr);                 /* Signal DHCPc initialization complete.                */

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_PEVENT_NULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_SEM_OVF:
        default:
            *perr = DHCPc_OS_ERR_INIT_SIGNALD;                  /* See Note #1a.                                        */
             break;
    }
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                   DHCPc LOCK MANAGEMENT FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                           DHCPc_OS_Lock()
*
* Description : Acquire mutually exclusive access to DHCP client.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               DHCPc access     acquired.
*                               DHCPc_OS_ERR_LOCK               DHCPc access NOT acquired.
*
* Return(s)   : none.
*
* Caller(s)   : various.
*
*               This function is an INTERNAL DHCP client function & SHOULD NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc access MUST be acquired--i.e. MUST wait for access; do NOT timeout.
*
*                   (a) Failure to acquire DHCPc access will prevent DHCPc task(s)/operation(s) from
*                       functioning.
*********************************************************************************************************
*/

void  DHCPc_OS_Lock (DHCPc_ERR  *perr)
{
    INT8U  os_err;


    OSSemPend((OS_EVENT *) DHCPc_OS_LockPtr,                    /* Acquire DHCPc access ...                             */
              (INT16U    ) 0,                                   /* ... without timeout (see Note #1).                   */
              (INT8U    *)&os_err);

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_PEVENT_NULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_PEND_ISR:
        case OS_ERR_PEND_LOCKED:
        case OS_ERR_PEND_ABORT:
        case OS_ERR_TIMEOUT:
        default:
            *perr = DHCPc_OS_ERR_LOCK;                          /* See Note #1a.                                        */
             break;
    }
}


/*
*********************************************************************************************************
*                                          DHCPc_OS_Unlock()
*
* Description : Release mutually exclusive access to DHCP client.
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : various.
*
*               This function is an INTERNAL DHCP client function & SHOULD NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc access MUST be released--i.e. MUST unlock access without failure.
*
*                   (a) Failure to release DHCPc access will prevent DHCPc task(s)/operation(s) from
*                       functioning.  Thus DHCPc access is assumed to be successfully released since
*                       NO uC/OS-II error handling could be performed to counteract failure.
*********************************************************************************************************
*/

void  DHCPc_OS_Unlock (void)
{
   (void)OSSemPost(DHCPc_OS_LockPtr);                           /* Release DHCPc access (see Note #1).                  */
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                   DHCPc TASK MANAGEMENT FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                         DHCPc_OS_TaskInit()
*
* Description : (1) Perform DHCPc Task/OS initialization :
*
*                   (a) Create DHCPc task
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               DHCPc task/OS initialization successful.
*                               DHCPc_OS_ERR_INIT_TASK          DHCPc task      NOT successfully initialized.
*                               DHCPc_OS_ERR_INIT_TASK_NAME     DHCPc task name NOT successfully configured.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TmrInit().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : none.
*********************************************************************************************************
*/
void  DHCPc_OS_TaskInit (DHCPc_ERR  *perr)
{
    INT8U  os_err;


                                                                /* Create DHCPc_OS_Task().                              */
#if (OS_TASK_CREATE_EXT_EN == 1)

#if (OS_STK_GROWTH == 1)
    os_err = OSTaskCreateExt((void (*)(void *)) DHCPc_OS_Task,
                             (void          * ) 0,
                             (OS_STK        * )&DHCPc_OS_TaskStk[DHCPc_OS_CFG_TASK_STK_SIZE - 1],
                             (INT8U           ) DHCPc_OS_CFG_TASK_PRIO,
                             (INT16U          ) DHCPc_OS_CFG_TASK_PRIO,         /* Set task id same as task prio.       */
                             (OS_STK        * )&DHCPc_OS_TaskStk[0],
                             (INT32U          ) DHCPc_OS_CFG_TASK_STK_SIZE,
                             (void          * ) 0,
                             (INT16U          )(OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK));
#else
    os_err = OSTaskCreateExt((void (*)(void *)) DHCPc_OS_Task,
                             (void          * ) 0,
                             (OS_STK        * )&DHCPc_OS_TaskStk[0],
                             (INT8U           ) DHCPc_OS_CFG_TASK_PRIO,
                             (INT16U          ) DHCPc_OS_CFG_TASK_PRIO,         /* Set task id same as task prio.       */
                             (OS_STK        * )&DHCPc_OS_TaskStk[DHCPc_OS_CFG_TASK_STK_SIZE - 1],
                             (INT32U          ) DHCPc_OS_CFG_TASK_STK_SIZE,
                             (void          * ) 0,
                             (INT16U          )(OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK));
#endif

#else

#if (OS_STK_GROWTH == 1)
    os_err = OSTaskCreate((void (*)(void *)) DHCPc_OS_Task,
                          (void          * ) 0,
                          (OS_STK        * )&DHCPc_OS_TaskStk[DHCPc_OS_CFG_TASK_STK_SIZE - 1],
                          (INT8U           ) DHCPc_OS_CFG_TASK_PRIO);
#else
    os_err = OSTaskCreate((void (*)(void *)) DHCPc_OS_Task,
                          (void          * ) 0,
                          (OS_STK        * )&DHCPc_OS_TaskStk[0],
                          (INT8U           ) DHCPc_OS_CFG_TASK_PRIO);
#endif

#endif
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_TASK;
        return;
    }


#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OS_TASK_NAME_SIZE_MAX)))
    OSTaskNameSet((INT8U  ) DHCPc_OS_CFG_TASK_PRIO,             /* Set the name of the task.                            */
                  (INT8U *) DHCPc_OS_TASK_NAME,
                  (INT8U *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_TASK_NAME;
        return;
    }
#endif


   *perr = DHCPc_OS_ERR_NONE;
}


/*
*********************************************************************************************************
*                                           DHCPc_OS_Task()
*
* Description : OS-dependent shell task to run DHCPc task.
*
* Argument(s) : p_data      Pointer to task initialization data (required by uC/OS-II).
*
* Return(s)   : none.
*
* Created by  : DHCPc_OS_TaskInit().
*
* Note(s)     : (1) DHCPc_OS_Task() blocked until DHCPc initialization completes.
*********************************************************************************************************
*/

static  void  DHCPc_OS_Task (void  *p_data)
{
   (void)&p_data;                                               /* Prevent 'variable unused' compiler warning.          */


    while (DEF_ON) {
        DHCPc_TaskHandler();
    }
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                 DHCPc MESSAGE MANAGEMENT FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                         DHCPc_OS_MsgWait()
*
* Description : Wait on message indicating DHCP action to be performed on an interface.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               Message received.
*                               DHCPc_OS_ERR_MSG_Q              Message NOT received.
*
* Return(s)   : Pointer to received message, if no error.
*
*               Pointer to NULL,             otherwise.
*
* Caller(s)   : DHCPc_TaskHandler().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc message from timer MUST be acquired--i.e. MUST wait for message; do NOT timeout.
*********************************************************************************************************
*/

void  *DHCPc_OS_MsgWait (DHCPc_ERR  *perr)
{
    void   *p_msg;
    INT8U   os_err;


    p_msg = OSQPend((OS_EVENT *) DHCPc_OS_MsgQPtr,              /* Wait on DHCPc queue ...                              */
                    (INT16U    ) 0,                             /* ... without timeout (see Note #1).                   */
                    (INT8U    *)&os_err);

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_PEVENT_NULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_PEND_ISR:
        case OS_ERR_PEND_LOCKED:
        case OS_ERR_PEND_ABORT:
        case OS_ERR_TIMEOUT:
        default:
            *perr = DHCPc_OS_ERR_MSG_Q;
             break;
    }

    return (p_msg);
}


/*
*********************************************************************************************************
*                                         DHCPc_OS_MsgPost()
*
* Description : Post a message indicating DHCP action to be performed on an interface.
*
* Argument(s) : pmsg            Pointer to message to post.
*
*               perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               Message     successfully posted.
*                               DHCPc_OS_ERR_MSG_Q              Message NOT successfully posted.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TmrTaskHandler(),
*               DHCPc_Start(),
*               DHCPc_Stop().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : none.
*********************************************************************************************************
*/

void  DHCPc_OS_MsgPost (void       *pmsg,
                        DHCPc_ERR  *perr)
{
    INT8U  os_err;


    os_err = OSQPost(DHCPc_OS_MsgQPtr, pmsg);                   /* Post message to message queue.                       */

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_Q_FULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_PEVENT_NULL:
        default:
            *perr = DHCPc_OS_ERR_MSG_Q;
             break;
    }
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                  DHCPc TIMER MANAGEMENT FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                         DHCPc_OS_TmrInit()
*
* Description : (1) Perform DHCPc Timer/OS initialization :
*
*                   (a) Create DHCPc timer
*                   (b) Create DHCPc timer signal
*                   (c) Create DHCPc timer task
*
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE                   DHCPc timer/OS initialization successful.
*
*                               DHCPc_OS_ERR_INIT_TMR               DHCPc timer                NOT successfully
*                                                                       initialized.
*
*                               DHCPc_OS_ERR_INIT_TMR_SIGNAL        DHCPc timer    signal      NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_SIGNAL_NAME   DHCPc timer    signal name NOT successfully
*                                                                       initialized.
*
*                               DHCPc_OS_ERR_INIT_TMR_TASK          DHCPc timer    task        NOT successfully
*                                                                       initialized.
*                               DHCPc_OS_ERR_INIT_TMR_TASK_NAME     DHCPc timer    task name   NOT successfully
*                                                                       configured.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TmrInit().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (2) The DHCPc timer's primary purpose is to schedule & run DHCPc_TmrTaskHandler(); the
*                   timer should have DHCPc_TmrTaskHandler() execute at every DHCPc_OS_TMR_PERIOD_SEC
*                   seconds forever (i.e. timer should NEVER stop running), and is doing that by signaling
*                   a semaphore which is being pended on by the DHCPc timer task.
*********************************************************************************************************
*/

void  DHCPc_OS_TmrInit (DHCPc_ERR  *perr)
{
    INT8U  os_err;


                                                                /* ----------------- CREATE DHCPc TMR ----------------- */
    DHCPc_OS_TmrPtr = OSTmrCreate((INT32U         ) 0,
                                  (INT32U         ) DHCPc_OS_TMR_PERIOD_TMR_TICK,
                                  (INT8U          ) OS_TMR_OPT_PERIODIC,
                                  (OS_TMR_CALLBACK) DHCPc_OS_TmrCallback,
                                  (void          *) 0,
                                  (INT8U         *) DHCPc_OS_TMR_NAME,
                                  (INT8U         *)&os_err);
    if (DHCPc_OS_TmrPtr == (OS_TMR *)0) {
       *perr = DHCPc_OS_ERR_INIT_TMR;
        return;
    }


                                                                /* ------------- CREATE DHCPc TMR SIGNAL -------------- */
    DHCPc_OS_TmrSignalPtr = OSSemCreate((INT16U)0);
    if (DHCPc_OS_TmrSignalPtr == (OS_EVENT *)0) {
       *perr = DHCPc_OS_ERR_INIT_TMR_SIGNAL;
        return;
    }

#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OBJ_NAME_SIZE_MAX)))
    OSEventNameSet((OS_EVENT *) DHCPc_OS_TmrSignalPtr,
                   (INT8U    *) DHCPc_OS_TMR_SIGNAL_NAME,
                   (INT8U    *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_TMR_SIGNAL_NAME;
        return;
    }
#endif


                                                                /* -------------- CREATE DHCPc TMR TASK --------------- */
#if (OS_TASK_CREATE_EXT_EN == 1)

#if (OS_STK_GROWTH == 1)
    os_err = OSTaskCreateExt((void (*)(void *)) DHCPc_OS_TmrTask,
                             (void          * ) 0,
                             (OS_STK        * )&DHCPc_OS_TmrTaskStk[DHCPc_OS_CFG_TMR_TASK_STK_SIZE - 1],
                             (INT8U           ) DHCPc_OS_CFG_TMR_TASK_PRIO,
                             (INT16U          ) DHCPc_OS_CFG_TMR_TASK_PRIO,     /* Set task id same as task prio.       */
                             (OS_STK        * )&DHCPc_OS_TmrTaskStk[0],
                             (INT32U          ) DHCPc_OS_CFG_TMR_TASK_STK_SIZE,
                             (void          * ) 0,
                             (INT16U          )(OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK));
#else
    os_err = OSTaskCreateExt((void (*)(void *)) DHCPc_OS_TmrTask,
                             (void          * ) 0,
                             (OS_STK        * )&DHCPc_OS_TmrTaskStk[0],
                             (INT8U           ) DHCPc_OS_CFG_TMR_TASK_PRIO,
                             (INT16U          ) DHCPc_OS_CFG_TMR_TASK_PRIO,     /* Set task id same as task prio.       */
                             (OS_STK        * )&DHCPc_OS_TmrTaskStk[DHCPc_OS_CFG_TMR_TASK_STK_SIZE - 1],
                             (INT32U          ) DHCPc_OS_CFG_TMR_TASK_STK_SIZE,
                             (void          * ) 0,
                             (INT16U          )(OS_TASK_OPT_STK_CLR | OS_TASK_OPT_STK_CHK));
#endif

#else

#if (OS_STK_GROWTH == 1)
    os_err = OSTaskCreate((void (*)(void *)) DHCPc_OS_TmrTask,
                          (void          * ) 0,
                          (OS_STK        * )&DHCPc_OS_TmrTaskStk[DHCPc_OS_CFG_TMR_TASK_STK_SIZE - 1],
                          (INT8U           ) DHCPc_OS_CFG_TMR_TASK_PRIO);
#else
    os_err = OSTaskCreate((void (*)(void *)) DHCPc_OS_TmrTask,
                          (void          * ) 0,
                          (OS_STK        * )&DHCPc_OS_TmrTaskStk[0],
                          (INT8U           ) DHCPc_OS_CFG_TMR_TASK_PRIO);
#endif

#endif
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_TMR_TASK;
        return;
    }


#if (((OS_VERSION >= 288) && (OS_EVENT_NAME_EN   >  0)) || \
     ((OS_VERSION <  288) && (OS_EVENT_NAME_SIZE >= DHCPc_OS_TASK_NAME_SIZE_MAX)))
    OSTaskNameSet((INT8U  ) DHCPc_OS_CFG_TMR_TASK_PRIO,         /* Set the name of the task.                            */
                  (INT8U *) DHCPc_OS_TMR_TASK_NAME,
                  (INT8U *)&os_err);
    if (os_err != OS_ERR_NONE) {
       *perr = DHCPc_OS_ERR_INIT_TMR_TASK_NAME;
        return;
    }
#endif


   *perr = DHCPc_OS_ERR_NONE;
}


/*
*********************************************************************************************************
*                                         DHCPc_OS_TmrTask()
*
* Description : OS-dependent shell task to run DHCPc timer task.
*
* Argument(s) : p_data      Pointer to task initialization data (required by uC/OS-II).
*
* Return(s)   : none.
*
* Created by  : DHCPc_OS_TmrInit().
*
* Note(s)     : (1) DHCPc_OS_TmrTask() blocked until DHCPc initialization completes.
*********************************************************************************************************
*/

static  void  DHCPc_OS_TmrTask (void  *p_data)
{
   (void)&p_data;                                               /* Prevent 'variable unused' compiler warning.          */


    while (DEF_ON) {
        DHCPc_TmrTaskHandler();
    }
}


/*
*********************************************************************************************************
*                                         DHCPc_OS_TmrStart()
*
* Description : Start the DHCPc Timer.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               DHCPc timer     successfully started.
*                               DHCPc_OS_ERR_TMR                DHCPc timer NOT successfully started.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_Init().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) The DHCPc timer MUST have been previously created by calling DHCPc_OS_TmrInit().
*********************************************************************************************************
*/

void  DHCPc_OS_TmrStart (DHCPc_ERR  *perr)
{
    BOOLEAN  tmr_started;
    INT8U    os_err;


    tmr_started = OSTmrStart(DHCPc_OS_TmrPtr, &os_err);
    if (tmr_started != OS_TRUE) {
       *perr = DHCPc_OS_ERR_TMR;
        return;
    }

   *perr = DHCPc_OS_ERR_NONE;
}


/*
*********************************************************************************************************
*                                       DHCPc_OS_TmrCallback()
*
* Description : DHCPc Timer callback function.
*
* Argument(s) : p_tmr       Pointer to         expiring timer (ignored).
*
*               p_arg       Argument passed by expiring timer (none).
*
* Return(s)   : none.
*
* Caller(s)   : Expiring DHCPc_OS_TMR_NAME Timer.
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : none.
*********************************************************************************************************
*/

static  void  DHCPc_OS_TmrCallback (void  *p_tmr,
                                    void  *p_arg)
{
   (void)&p_tmr;                                                /* Prevent 'variable unused' compiler warnings.         */
   (void)&p_arg;

    DHCPc_OS_TmrSignal();                                       /* Signal DHCPc timer expired.                          */
}


/*
*********************************************************************************************************
*                                         DHCPc_OS_TmrWait()
*
* Description : Wait on signal indicating DHCPc timer expired.
*
* Argument(s) : perr        Pointer to variable that will receive the return error code from this function :
*
*                               DHCPc_OS_ERR_NONE               Timer signal     received.
*                               DHCPc_OS_ERR_TMR                Timer signal NOT received.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_TmrTaskHandler().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc timer signal MUST be acquired--i.e. MUST wait for access; do NOT timeout.
*********************************************************************************************************
*/

void  DHCPc_OS_TmrWait (DHCPc_ERR  *perr)
{
    INT8U  os_err;


    OSSemPend((OS_EVENT *) DHCPc_OS_TmrSignalPtr,               /* Wait until DHCPc timer expires ...                   */
              (INT16U    ) 0,                                   /* ... without timeout (see Note #1).                   */
              (INT8U    *)&os_err);

    switch (os_err) {
        case OS_ERR_NONE:
            *perr = DHCPc_OS_ERR_NONE;
             break;


        case OS_ERR_PEVENT_NULL:
        case OS_ERR_EVENT_TYPE:
        case OS_ERR_PEND_ISR:
        case OS_ERR_PEND_LOCKED:
        case OS_ERR_PEND_ABORT:
        case OS_ERR_TIMEOUT:
        default:
            *perr = DHCPc_OS_ERR_TMR;
             break;
    }
}


/*
*********************************************************************************************************
*                                        DHCPc_OS_TmrSignal()
*
* Description : Signal that DHCPc timer expired.
*
* Argument(s) : none.
*
* Return(s)   : none.
*
* Caller(s)   : DHCPc_OS_TmrCallback().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) DHCPc timer MUST be signaled--i.e. MUST signal without failure.
*
*                   (a) Failure to signal will prevent DHCPc timer task from running.
*********************************************************************************************************
*/

void  DHCPc_OS_TmrSignal (void)
{
   (void)OSSemPost(DHCPc_OS_TmrSignalPtr);                      /* Signal DHCPc timer expired (see Note #1).            */
}


/*
*********************************************************************************************************
*********************************************************************************************************
*                                       DHCPc/OS TIME FUNCTIONS
*********************************************************************************************************
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*                                       DHCPc_OS_TimeGet_tick()
*
* Description : Get the current time value.
*
* Argument(s) : none.
*
* Return(s)   : Number of clock ticks elapsed since OS startup.
*
* Caller(s)   : DHCPc_TxMsgPrepare(),
*               DHCPc_LeaseTimeCalc(),
*               DHCPc_TmrTaskHandler().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) The value returned by this function OS configuration dependent (number of ticks per
*                   second), and hence cannot be interpreted directly by the caller.
*
*                   See 'DHCPc_OS_TimeCalcElapsed_sec()' for useful usage.
*********************************************************************************************************
*/

CPU_INT32U  DHCPc_OS_TimeGet_tick (void)
{
    INT32U  time_cur;


    time_cur = OSTimeGet();

    return ((CPU_INT32U)time_cur);
}


/*
*********************************************************************************************************
*                                   DHCPc_OS_TimeCalcElapsed_sec()
*
* Description : Calculate the number of seconds elapsed between start and stop time.
*
* Argument(s) : time_start      Start time (in clock ticks).
*
*               time_stop       Stop  time (in clock ticks).
*
* Return(s)   : Number of seconds elapsed, if NO errors.
*
*               0,                         otherwise.
*
* Caller(s)   : DHCPc_LeaseTimeCalc().
*
*               This function is an INTERNAL DHCP client function & MUST NOT be called by application
*               function(s).
*
* Note(s)     : (1) The values of the 'time_start' & 'time_stop' parameters are obtained from a call to
*                   DHCPc_OS_TimeGet_tick().  Those values are clock tick dependent, and are converted
*                   in units of seconds by this function.
*
*               (2) Elapsed time delta calculation adjusts ONLY for a single overflow time ticks.  Thus
*                   if elapsed time is greater than the maximum 32-bit overflow threshold, then elapsed
*                   time will incorrectly calculate a lower elaspsed time.  However, this should NEVER
*                   occur since the maximum 32-bit overflow threshold for times measured in seconds is
*                   136.2 years.
*********************************************************************************************************
*/

CPU_INT32U  DHCPc_OS_TimeCalcElapsed_sec (CPU_INT32U  time_start,
                                          CPU_INT32U  time_stop)
{
#if (OS_TICKS_PER_SEC > 0u)
    CPU_INT32U  time_delta_tick;
    CPU_INT32U  time_sec;

                                                                /* Calculate delta time (in ticks) [see Note #2].       */
    if (time_start <= time_stop) {
        time_delta_tick = time_stop - time_start;
    } else {                                                    /* If stop time > start time, adjust for tick overflow. */
        time_delta_tick = ((DEF_INT_32U_MAX_VAL - time_start) + 1u) + time_stop;
    }

    time_sec = time_delta_tick / OS_TICKS_PER_SEC;              /* Calculate time (in seconds).                         */

    return (time_sec);

#else
   (void)&time_start;                                           /* Prevent 'variable unused' compiler warnings.         */
   (void)&time_stop;

    return (0u);
#endif
}

