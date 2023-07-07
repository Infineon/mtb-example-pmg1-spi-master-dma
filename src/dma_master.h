/******************************************************************************
 * File Name: dma_master.h
 *
 * Description: Header file for DMA operation.
 *
 * Related Document: See README.md
 *
 *******************************************************************************
 * Copyright 2023, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

#ifndef SOURCE_DMA_MASTER_H_
#define SOURCE_DMA_MASTER_H_

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cy_pdl.h"
#include "cycfg.h"
#include "status.h"

/*******************************************************************************
* Macros
********************************************************************************/
#define RXDMA_CHANNEL_INT_MASK    (CY_DMAC_INTR_CHAN_0)
#define TXDMA_CHANNEL_INT_MASK    (CY_DMAC_INTR_CHAN_1)

/******************************************************************************
 * Structure/Enum type declaration
 ******************************************************************************/
/**
* Configuration structure for a single DMA transfer. Takes a source and
* destination buffer defined by user, and the number of bytes to transfer.
*
* Note: FIFO of specific src/dst is configured in the DMA descriptor by
* init function.
* 
* */
typedef struct
{
    uint8_t*        src;    /* Source buffer */
    uint8_t*        dst;    /* Destination buffer */
    uint16_t    num_bytes;  /* Number of bytes to transfer */
} dma_master_packet_t;

/* Type for callback function exectued as part of DMA interrupt after
 * completion. */
typedef bool (*callback_dma_completion)(void);

/******************************************************************************
 * Global function declaration
 ******************************************************************************/
uint32_t dma_init(void *wr, void *rd, callback_dma_completion cb);
void send_packet(dma_master_packet_t *pong);
void send_packet_multi(dma_master_packet_t *ping, dma_master_packet_t *pong);
bool dma_state_done(void);
bool dma_has_error(void);
void dma_state_reset(void);
cy_rslt_t dma_get_error(void);

#endif /* SOURCE_DMA_MASTER_H_ */
