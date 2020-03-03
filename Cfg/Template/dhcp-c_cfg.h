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
*                                   DHCP CLIENT CONFIGURATION FILE
*
*                                              TEMPLATE
*
* Filename : dhcp-c_cfg.h
* Version  : V2.11.00
*********************************************************************************************************
*/


/*
*********************************************************************************************************
*                                           TASKS PRIORITIES
* Notes: (1) Task priorities configuration values should be used by the DHCPc OS port. The following task priorities
*            should be defined:
*
*                   DHCPc_OS_CFG_TASK_PRIO
*                   DHCPc_OS_CFG_TMR_TASK_PRIO
*
*            Task priorities can be defined either in this configuration file 'dhcp-c_cfg.h' or in a global
*            OS tasks priorities configuration header file which must be included in 'dhcp-c_cfg.h'.
*********************************************************************************************************
*/

                                                                /* See Note #1.                                         */
#define  DHCPc_OS_CFG_TASK_PRIO                           13
#define  DHCPc_OS_CFG_TMR_TASK_PRIO                       14


/*
*********************************************************************************************************
*                                              STACK SIZES
*            Size (depth) of the task stacks (See the definition of CPU_STK for stack width)
*********************************************************************************************************
*/

#define  DHCPc_OS_CFG_TASK_STK_SIZE                      512
#define  DHCPc_OS_CFG_TMR_TASK_STK_SIZE                  512


/*
*********************************************************************************************************
*                                                 DHCPc
*
* Note(s) : (1) Default port for DHCP server is 67, and default port for DHCP client is 68.
*
*           (3) Configure DHCPc_CFG_MAX_NBR_IF to the maximum number of interface this DHCP client will
*               be able to manage at a given time.
*
*           (4) Once the DHCP server has assigned the client an address, the later may perform a final
*               check prior to use this address in order to make sure it is not being used by another
*               host on the network.
*********************************************************************************************************
*/

#define  DHCPc_CFG_IP_PORT_SERVER                         67    /* Configure DHCP server port            (see Note #1). */
#define  DHCPc_CFG_IP_PORT_CLIENT                         68    /* Configure DHCP client port            (see Note #1). */

#define  DHCPc_CFG_MAX_RX_TIMEOUT_MS                    5000    /* Maximum inactivity time (ms) on receive.             */

#define  DHCPc_CFG_PARAM_REQ_TBL_SIZE                      5    /* Configure requested parameter table size.            */

#define  DHCPc_CFG_MAX_NBR_IF                              1    /* Configure maximum number of interface (see Note #3). */

#define  DHCPc_CFG_ADDR_VALIDATE_EN              DEF_ENABLED    /* Configure final check on assigned address ...        */
                                                                /* ... (see Note #4) :                                  */
                                                                /*   DEF_DISABLED  Validation NOT performed             */
                                                                /*   DEF_ENABLED   Validation     performed             */

#define  DHCPc_CFG_DYN_LOCAL_LINK_ADDR_EN        DEF_ENABLED    /* Configure dynamic link-local address configuration : */
                                                                /*   DEF_DISABLED  local-link configuration DISABLED    */
                                                                /*   DEF_ENABLED   local-link configuration ENABLED     */

#define  DHCPc_CFG_LOCAL_LINK_MAX_RETRY                    3    /* Configure maximum number of retry to get a           */
                                                                /* link-local address.                                  */

#define  DHCPc_CFG_NEGO_RETRY_CNT                          3    /* Number of lease negotiation retries                  */

#define  DHCPc_CFG_DISCOVER_RETRY_CNT                      3    /* Number of attempts to transmit discover messages     */
                                                                /* during the discover phase of the lease negotiation   */

#define  DHCPc_CFG_REQUEST_RETRY_CNT                       3    /* Number of attemps to transmit request messages       */
                                                                /* during the request phase of the lease negotiation    */


/*
*********************************************************************************************************
*                                 DHCPc ARGUMENT CHECK CONFIGURATION
*
* Note(s) : (1) Configure DHCPc_CFG_ARG_CHK_EXT_EN to enable/disable the DHCP client external argument
*               check feature :
*
*               (a) When ENABLED,  ALL arguments received from any port interface provided by the developer
*                   or application are checked/validated.
*
*               (b) When DISABLED, NO  arguments received from any port interface provided by the developer
*                   or application are checked/validated.
*
*           (2) Configure DHCPc_CFG_ARG_CHK_DBG_EN to enable/disable the DHCP client internal debug
*               argument check feature :
*
*               (a) When ENABLED,     internal arguments are checked/validated to debug the DHCP client.
*
*               (b) When DISABLED, NO internal arguments are checked/validated to debug the DHCP client.
*
*           (3) Configure DHCPc_DBG_CFG_MEM_CLR_EN to enable/disable the DHCP client from clearing
*               internal data structure memory buffers; a convenient feature while debugging.
*********************************************************************************************************
*/
                                                                /* Configure external argument check feature ...        */
                                                                /* ... (see Note #1) :                                  */
#define  DHCPc_CFG_ARG_CHK_EXT_EN                DEF_ENABLED
                                                                /*   DEF_DISABLED     Argument check DISABLED           */
                                                                /*   DEF_ENABLED      Argument check ENABLED            */

                                                                /* Configure internal argument check feature ...        */
                                                                /* ... (see Note #2) :                                  */
#define  DHCPc_CFG_ARG_CHK_DBG_EN                DEF_DISABLED
                                                                /*   DEF_DISABLED     Argument check DISABLED           */
                                                                /*   DEF_ENABLED      Argument check ENABLED            */

                                                                /* Configure memory clear feature  (see Note #3) :      */
#define  DHCPc_DBG_CFG_MEM_CLR_EN                DEF_ENABLED
                                                                /*   DEF_DISABLED     Data structure clears DISABLED    */
                                                                /*   DEF_ENABLED      Data structure clears ENABLED     */

