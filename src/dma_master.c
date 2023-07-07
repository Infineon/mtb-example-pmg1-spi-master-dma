/******************************************************************************
 * File Name: dma_master.c
 *
 * Description: Source file for DMA operation.
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

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "dma_master.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define DMA_IRQ               (cpuss_interrupt_dma_IRQn)
#define DMA_INT_PRIORITY      (3u)

/*******************************************************************************
 * Global variables declaration
 ******************************************************************************/
static cy_stc_sysint_t DMA_int_cfg =
{
        .intrSrc      = (IRQn_Type)DMA_IRQ,
        .intrPriority = DMA_INT_PRIORITY,
};

static bool is_init = false;
volatile bool rx_dma_done = false;
volatile bool tx_dma_error = false;
volatile bool extern_done = false;
volatile bool rx_dma_error = false;
volatile bool tx_dma_done= false;

static bool cb_dma_dummy(void)
{
    return true;
}
callback_dma_completion cb_dma = cb_dma_dummy;

/* Internal functions */
static void send_packet_internal(uint8_t* tx_ping, uint8_t* rx_ping,
        uint32_t num_bytes_ping, uint8_t* tx_pong, uint8_t* rx_pong, uint32_t num_bytes_pong);
static void tx_dma_complete(void);

/******************************************************************************
* Function Name: init_dma_master
*******************************************************************************
*
* Summary:
*  This function initializes the DMA Master based on the
*  configuration done in design.modus file. PING and PONG
*  descriptors are initialized for TX- and RX-channels,
*  their use is up to operation.
*
* Parameters:
*  wr: Pointer for source data to copy to (i.e. SPI-FIFO-TX)
*  rd: pointer for destination data to copy from (i.e. SPI-FIFO-RX)
*  cb: Pointer to function which executes as part of the DMA-
*  completion interrupt. Can be NULL, then it's function is ignored.
*
* Return:
*  (uint32_t) INIT_SUCCESS or INIT_FAILURE
*
******************************************************************************/
uint32_t dma_init(void *wr, void *rd, callback_dma_completion cb)
{
    cy_en_dmac_status_t dmac_init_status;

    /* Register callback function */
    if(cb != NULL)
    {
        cb_dma = cb;
    }

    tx_dma_done = true;
    rx_dma_done = true;
    tx_dma_error = false;
    rx_dma_error = false;
    extern_done = true;

    /* Initialize descriptors */
    dmac_init_status = Cy_DMAC_Descriptor_Init(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, &txDma_ping_config);
    dmac_init_status |= Cy_DMAC_Descriptor_Init(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, &txDma_pong_config);
    dmac_init_status |= Cy_DMAC_Descriptor_Init(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, &rxDma_ping_config);
    dmac_init_status |= Cy_DMAC_Descriptor_Init(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, &rxDma_pong_config);

    if (dmac_init_status!=CY_DMAC_SUCCESS)
        return INIT_FAILURE;

    /* Initialize channels */
    dmac_init_status = Cy_DMAC_Channel_Init(txDma_HW, txDma_CHANNEL, &txDma_channel_config);
    dmac_init_status |= Cy_DMAC_Channel_Init(rxDma_HW, rxDma_CHANNEL, &rxDma_channel_config);

    if (dmac_init_status!=CY_DMAC_SUCCESS)
        return INIT_FAILURE;

    /* Set destination for all descriptors */
    Cy_DMAC_Descriptor_SetDstAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, wr);
    Cy_DMAC_Descriptor_SetDstAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, wr);
    Cy_DMAC_Descriptor_SetSrcAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, rd);
    Cy_DMAC_Descriptor_SetSrcAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, rd);

    /* Enable interrupt for TxDma channel and RxDma channel */
    Cy_DMAC_SetInterruptMask(txDma_HW, TXDMA_CHANNEL_INT_MASK | RXDMA_CHANNEL_INT_MASK);
    Cy_DMAC_SetInterruptMask(rxDma_HW, TXDMA_CHANNEL_INT_MASK | RXDMA_CHANNEL_INT_MASK);

    /* Initialize and enable the interrupt from TxDma */
    Cy_SysInt_Init(&DMA_int_cfg, &tx_dma_complete);
    NVIC_EnableIRQ(DMA_int_cfg.intrSrc);

    /* Initialization completed */
    is_init = true;

    return(INIT_SUCCESS);
}

/******************************************************************************
* Function Name: send_packet
*******************************************************************************
*
* Summary:
*  Public function to send one data packet as DMA transfer.
*  This function enables PONG-descriptor for transfer.
*
* Parameters:
*  pong: data for descriptor to send
*
* Return:
*  None
*
******************************************************************************/
void send_packet(dma_master_packet_t *pong)
{
    if(!is_init || !tx_dma_done || !rx_dma_done)
    {
        return;
    }

    /* Set PING descriptor as current descriptor for TxDma and RxDma channel  */
    Cy_DMAC_Channel_SetCurrentDescriptor(txDma_HW, txDma_CHANNEL,
                                         CY_DMAC_DESCRIPTOR_PONG);
    Cy_DMAC_Channel_SetCurrentDescriptor(rxDma_HW, rxDma_CHANNEL,
                                         CY_DMAC_DESCRIPTOR_PONG);
    /* Validate the all descriptors, this makes them ONE-TIME-USABLE. If there is random data floating,
     * i.e into the rx-FIFO, nothing will happen. */
    Cy_DMAC_Descriptor_SetState(txDma_HW, txDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PING, false);
    Cy_DMAC_Descriptor_SetState(txDma_HW, txDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PONG, true);
    Cy_DMAC_Descriptor_SetState(rxDma_HW, rxDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PING, false);
    Cy_DMAC_Descriptor_SetState(rxDma_HW, rxDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PONG, true);

    send_packet_internal(NULL, NULL, 1, pong->src, pong->dst, pong->num_bytes);
}

/******************************************************************************
* Function Name: send_packet_multi
*******************************************************************************
*
* Summary:
*  Public function to send two data packets as DMA transfer.
*  This function enables both descriptors for transfer.
*
* Parameters:
*  ping: data for first descriptor to send
*  pong: data for second descriptor to send
*
* Return:
*  None
*
******************************************************************************/
void send_packet_multi(dma_master_packet_t *ping, dma_master_packet_t *pong)
{
    if(!is_init || !tx_dma_done || !rx_dma_done)
    {
        return;
    }

    /* Set PING descriptor as current descriptor for TxDma and RxDma channel  */
    Cy_DMAC_Channel_SetCurrentDescriptor(txDma_HW, txDma_CHANNEL,
                                         CY_DMAC_DESCRIPTOR_PING);
    Cy_DMAC_Channel_SetCurrentDescriptor(rxDma_HW, rxDma_CHANNEL,
                                         CY_DMAC_DESCRIPTOR_PING);
    /* Validate the all descriptors, this makes them ONE-TIME-USABLE. If there is random data floating,
     * i.e into the rx-FIFO, nothing will happen. */
    Cy_DMAC_Descriptor_SetState(txDma_HW, txDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PING, true);
    Cy_DMAC_Descriptor_SetState(txDma_HW, txDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PONG, true);
    Cy_DMAC_Descriptor_SetState(rxDma_HW, rxDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PING, true);
    Cy_DMAC_Descriptor_SetState(rxDma_HW, rxDma_CHANNEL,
                                             CY_DMAC_DESCRIPTOR_PONG, true);

    send_packet_internal(ping->src, ping->dst, ping->num_bytes, pong->src, pong->dst, pong->num_bytes);
}

/******************************************************************************
* Function Name: send_packet_internal
*******************************************************************************
*
* Summary:
*  Send two data packets as continuous DMA transfer. (When used with SPI
*  this will create a continuous transfer without CS toggling.) This function
*  takes parameters as plain pointers to data and number of bytes to send
*  and is therefore not intended to be used directly by the user.
*
* Parameters:
*  tx_ping: pointer to data for first sending descriptor to send
*  rx_ping: pointer to data for first receiving descriptor
*  num_bytes_ping: number of bytes to send for first descriptor
*  tx_pong: pointer to data for second sending descriptor to send
*  rx_pong: pointer to data for second receiving descriptor
*  num_bytes_pong: number of bytes to send for second descriptor
*
* Return:
*  None
*
******************************************************************************/
static void send_packet_internal(uint8_t* tx_ping, uint8_t* rx_ping,
        uint32_t num_bytes_ping, uint8_t* tx_pong, uint8_t* rx_pong, uint32_t num_bytes_pong)
{
    rx_dma_done = false;
    rx_dma_done = false;
    extern_done = false;
    const static uint8_t tx_default = CY_SCB_SPI_DEFAULT_TX&0xFF;
    static uint8_t rx_default;

    /* Set source and destination for all descriptors */
    if(tx_ping != NULL)
    {
        Cy_DMAC_Descriptor_SetSrcAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, (void *) tx_ping);
        Cy_DMAC_Descriptor_SetSrcIncrement(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, true);
    }
    else
    {
        Cy_DMAC_Descriptor_SetSrcAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, (void *) &tx_default);
        Cy_DMAC_Descriptor_SetSrcIncrement(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, false);
    }

    if(tx_pong != NULL)
    {
        Cy_DMAC_Descriptor_SetSrcAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, (void *) tx_pong);
        Cy_DMAC_Descriptor_SetSrcIncrement(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, true);
    }
    else
    {
        Cy_DMAC_Descriptor_SetSrcAddress(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, (void *) &tx_default);
        Cy_DMAC_Descriptor_SetSrcIncrement(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, false);
    }

    if(rx_ping != NULL)
    {
        Cy_DMAC_Descriptor_SetDstAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING,(void *) rx_ping);
        Cy_DMAC_Descriptor_SetDstIncrement(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, true);
    }
    else
    {
        Cy_DMAC_Descriptor_SetDstAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING,(void *) &rx_default);
        Cy_DMAC_Descriptor_SetDstIncrement(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, false);
    }

    if(rx_pong != NULL)
    {
        Cy_DMAC_Descriptor_SetDstAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG,(void *) rx_pong);
        Cy_DMAC_Descriptor_SetDstIncrement(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, true);
    }
    else
    {
        Cy_DMAC_Descriptor_SetDstAddress(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG,(void *) &rx_default);
        Cy_DMAC_Descriptor_SetDstIncrement(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, false);
    }

    /* Set number of bytes we want to read/write */
    Cy_DMAC_Descriptor_SetDataCount(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, num_bytes_ping);
    Cy_DMAC_Descriptor_SetDataCount(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PING, num_bytes_ping);
    Cy_DMAC_Descriptor_SetDataCount(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, num_bytes_pong);
    Cy_DMAC_Descriptor_SetDataCount(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG, num_bytes_pong);

    /* Enable DMA channel to transfer bytes */
    Cy_DMAC_Channel_Enable(rxDma_HW, rxDma_CHANNEL);
    Cy_DMAC_Enable(rxDma_HW);
    Cy_DMAC_Channel_Enable(txDma_HW, txDma_CHANNEL);
    Cy_DMAC_Enable(txDma_HW);
}

/******************************************************************************
* Function Name: tx_dma_complete
*******************************************************************************
*
* Summary:
*  Interrupt callback on DMA completion. Executed once for TX- and
*  once for RX-transfer. After both transfers complete successfully,
*  internal state changes to true and cb_dma_dummy is called for
*  eventual further processing.
*
* Parameters:
*  None
*
* Return:
*  None
*
******************************************************************************/
void tx_dma_complete(void)
{
    cy_en_dmac_response_t dmac_response;

    if ((Cy_DMAC_GetInterruptStatusMasked(txDma_HW) & TXDMA_CHANNEL_INT_MASK) != 0)
    {
        dmac_response = Cy_DMAC_Descriptor_GetResponse(txDma_HW, txDma_CHANNEL,
                                                       CY_DMAC_DESCRIPTOR_PONG);

        /* Check if the TxDma channel response is successful for current
         * transfer. Note that current descriptor is set to invalid after
         * completion */
        if ((dmac_response != CY_DMAC_DONE) && (dmac_response != CY_DMAC_INVALID_DESCR))
        {
            tx_dma_error = true;
        }
        else if (dmac_response == CY_DMAC_INVALID_DESCR)
        {
            tx_dma_done = true;
        }
        /* Clear TxDma channel interrupt */
        Cy_DMAC_ClearInterrupt(txDma_HW, TXDMA_CHANNEL_INT_MASK);
    }

    if ((Cy_DMAC_GetInterruptStatusMasked(rxDma_HW) & RXDMA_CHANNEL_INT_MASK) != 0)
    {
        dmac_response = Cy_DMAC_Descriptor_GetResponse(rxDma_HW, rxDma_CHANNEL,
                                                       CY_DMAC_DESCRIPTOR_PONG);

        /* Check if the RxDma channel response is successful for current
         * transfer. Note that current descriptor is set to invalid after
         * completion */
        if(CY_DMAC_DONE == dmac_response)
        {
            rx_dma_done = true;
        }
        else
        {
            rx_dma_error = true;
        }
        /* Clear TxDma channel interrupt */
        Cy_DMAC_ClearInterrupt(rxDma_HW, RXDMA_CHANNEL_INT_MASK);
    }
    /* User callback */
    if(rx_dma_done & tx_dma_done)
    {
        extern_done = cb_dma();
    }
}

/*******************************************************************************
* Function Name: DMA_is_done
********************************************************************************
*
* Summary:
*  Return DMA finished state.
*
* Parameters:
*  None
*
* Return:
*  (bool) true if DMA is done, false otherwise.
*
*******************************************************************************/
bool dma_state_done(void)
{
    return rx_dma_done & tx_dma_done & extern_done;
}

/*******************************************************************************
* Function Name: dma_has_error
********************************************************************************
*
* Summary:
*  Return DMA error state.
*
* Parameters:
*  None
*
* Return:
*  (bool) true if DMA occured internal error, false otherwise.
*
*******************************************************************************/
bool dma_has_error(void)
{
    return rx_dma_error | tx_dma_error;
}

/*******************************************************************************
* Function Name: dma_has_error
********************************************************************************
*
* Summary:
*  Return DMAC interrupt error from DMA.
*
* Parameters:
*  None
*
* Return:
*  (cy_rslt_t) The cause from Cy_DMAC_Descriptor_GetResponse of the interrupt.
*
*******************************************************************************/
cy_rslt_t dma_get_error(void)
{
    /* CY_DMAC_DONE and CY_DMAC_INVALID_DESCR are always set after a transmission, we ignore them */
    return (Cy_DMAC_Descriptor_GetResponse(rxDma_HW, rxDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG) |
            Cy_DMAC_Descriptor_GetResponse(txDma_HW, txDma_CHANNEL, CY_DMAC_DESCRIPTOR_PONG)
            ) & ~(CY_DMAC_DONE | CY_DMAC_INVALID_DESCR);
}

/*******************************************************************************
* Function Name: dma_state_reset
********************************************************************************
*
* Summary:
*  Wait for any transfer to finish. Then reset internal DMA states.
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void dma_state_reset(void)
{
    while((!tx_dma_done && !tx_dma_error) || (!rx_dma_done && !rx_dma_error));

    rx_dma_done = true;
    tx_dma_done = true;
    rx_dma_error = false;
    tx_dma_error = false;
}

/* [] END OF FILE */
