/******************************************************************************
 * File Name: spi_eeprom_master.c
 *
 * Description: Source file for SPI master interface to SPI EEPROM slave.
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
#include "spi_eeprom_master.h"
#include "cy_sysint.h"
#include "cy_scb_spi.h"
#include "dma_master.h"

/*******************************************************************************
 * Macros
 *******************************************************************************/
#ifndef SET_EEPROM_ADDRESS_TYPE
#error Requires address size of EEPROM
#elif SET_EEPROM_ADDRESS_TYPE == EEPROM_ADDRESS_TYPE_8
#define SPI_FLASH_CMD_MAX_SIZE (2)
#define POPULATE_COMMAND_ADDRESS(cmd, addr) \
    {cmd_pkt[0] = cmd; cmd_pkt[1] = ((addr))&0xFF;}
#elif SET_EEPROM_ADDRESS_TYPE == EEPROM_ADDRESS_TYPE_16
#define SPI_FLASH_CMD_MAX_SIZE (3)
#define POPULATE_COMMAND_ADDRESS(cmd, addr) \
    {cmd_pkt[0] = cmd; cmd_pkt[1] = ((addr)>>8)&0xFF; \
    cmd_pkt[2] = ((addr))&0xFF;}
#elif SET_EEPROM_ADDRESS_TYPE == EEPROM_ADDRESS_TYPE_24
#define SPI_FLASH_CMD_MAX_SIZE (4)
#define POPULATE_COMMAND_ADDRESS(cmd, addr) \
    {cmd_pkt[0] = cmd; cmd_pkt[1] = ((addr)>>16)&0xFF; \
    cmd_pkt[2] = ((addr)>>8)&0xFF; cmd_pkt[3] = ((addr))&0xFF;}
#elif SET_EEPROM_ADDRESS_TYPE == EEPROM_ADDRESS_TYPE_32
#define SPI_FLASH_CMD_MAX_SIZE (5)
#define POPULATE_COMMAND_ADDRESS(cmd, addr) \
    {cmd_pkt[0] = cmd; cmd_pkt[1] = ((addr)>>24)&0xFF; \
    cmd_pkt[2] = ((addr)>>16)&0xFF; cmd_pkt[3] = ((addr)>>8)&0xFF; \
    cmd_pkt[4] = ((addr))&0xFF;}
#endif

/*******************************************************************************
* Global variables declaration
*******************************************************************************/
/* Structure for SPI context */
static cy_stc_scb_spi_context_t flash_spi_context;

/* Buffer for command and address */
static uint8_t cmd_pkt[SPI_FLASH_CMD_MAX_SIZE];

/* Buffer for DMA structure */
static dma_master_packet_t ping, pong;

/* Copy of status register 1 in bg_status.status, used to determine IDLE state or waiting on WIP */
static struct
{
    uint8_t placeholder;
    uint8_t status;
} bg_status;

/*******************************************************************************
 * Function Name: dmaCompletionCallback
 *******************************************************************************
 *
 * Summary:
 *  This function is executed as part of the DMA interrupt. With this additional
 *  checks may be performed or transfer functions started.
 * 
 *  In this case, the backgorund status register is checked to see if the
 *  write is complete. As long as this is not the case, another DMA transfer
 *  is triggered to poll the status register.
 * 
 * Parameters:
 *  bg_status: Global variable with backup of status.
 *
 * Return:
 *  (bool) True if no more data to be transferred.
 *  Otherwise it returns false.
 *
 ******************************************************************************/
bool dma_completion_cb(void)
{
    if (SPI_EEPROM_IS_WRITE_IN_PROGRESS(bg_status.status))
    {
        static uint8_t cmd[RD_STATUS_SINGULAR_LEN] = {FLASH_READ_STATUS, 0};
        pong = (dma_master_packet_t)
        {
            .src = cmd,
            .dst = (uint8_t*)&bg_status,
            .num_bytes = RD_STATUS_SINGULAR_LEN, /* Using one buffer with full length each */
        };
        send_packet(&pong);
        return false;
    }

    /* Everything related to this r/w is done */
    return true;
}

/*******************************************************************************
 * Function Name: spi_eeprom_init
 *******************************************************************************
 *
 * Summary:
 *  This function initializes the SPI master based on the configuration done in
 *  design.modus file and calls initialize for DMA.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  (eeprom_dma_status_t) Returns INIT_SUCCESS if the initialization is successful.
 *  Otherwise it returns INIT_FAILURE
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_init(void)
{
    cy_rslt_t result = Cy_SCB_SPI_Init(FLASH_SPI_HW, &FLASH_SPI_config, &flash_spi_context);
    if (result != CY_SCB_SPI_SUCCESS)
    {
        return INIT_FAILURE;
    }

    /* Enable the SPI Master block */
    Cy_SCB_SPI_Enable(FLASH_SPI_HW);

    result = dma_init((void *) &(FLASH_SPI_HW->TX_FIFO_WR), (void *) &(FLASH_SPI_HW->RX_FIFO_RD),
            &dma_completion_cb);
    if (result != INIT_SUCCESS)
    {
        return INIT_FAILURE;
    }
    return INIT_SUCCESS;
}


/*******************************************************************************
 * Function Name: spi_eeprom_rdid_reg
 *******************************************************************************
 *
 * Summary:
 *  Read SPI EEPROM RDID register.
 *
 * Parameters:
 *  data Pointer to EEPROM RDID register value.
 *  data_len Number of bytes to be read from teh register.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_rdid_reg(uint8_t *data, uint8_t data_len)
{   
    /* Create RDID command packet. */
    cmd_pkt[0] = FLASH_RDID;

    return spi_master_read_write_array (NULL, data, data_len, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_read_status_reg
 *******************************************************************************
 *
 * Summary:
 *  Read SPI EEPROM status register.
 *
 * Parameters:
 *  status Variable to hold EEPROM status register value.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_read_status_reg(uint8_t *status)
{
    /* Create READ_STATUS command packet. */
    cmd_pkt[0] = FLASH_READ_STATUS;

    return spi_master_read_write_array (NULL, status, RD_STATUS_DATA_LEN, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_read_status_reg
 *******************************************************************************
 *
 * Summary:
 *  Read SPI EEPROM status register 2.
 *
 * Parameters:
 *  status Variable to hold EEPROM status register 2 value.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_read_status_2_reg(uint8_t *status)
{
    /* Create READ_STATUS command packet. */
    cmd_pkt[0] = FLASH_READ_STATUS_2;

    return spi_master_read_write_array (NULL, status, RD_STATUS_DATA_LEN, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_read_status_reg
 *******************************************************************************
 *
 * Summary:
 *  Read SPI EEPROM configuration register.
 *
 * Parameters:
 *  rd_config Variable to hold EEPROM rd_config register value.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_read_config_reg(uint8_t *rd_config)
{
    /* Create READ_CONFIG command packet. */
    cmd_pkt[0] = FLASH_READ_CONFIG;

    return spi_master_read_write_array (NULL, rd_config, RD_STATUS_DATA_LEN, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_write_status_reg
 *******************************************************************************
 *
 * Summary:
 *  Write SPI EEPROM status/configuration register.
 *  Status/Configuration register is written to set/clear HPM in the EEPROM.
 *
 * Parameters:
 *  srwd_block_write_prot_en This parameter indicates if SRWD needs to
 *                           be set and all blocks needs to be
 *                           protected in the SPI Flash. This will
 *                           prepare the SPI Flash for Hardware
 *                           Protection Mode.
 *
 * Notes:
 *  1. EEPROM write protect GPIO is not handled in this function.
 *  2. The value of status register is different for different flashes.
 *    So this API needs to be changed accordingly to suite the EEPROM requirements.
 * 
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_write_status_reg(bool srwd_block_write_prot_en)
{
    /* Create WRSR (Write Status Register) command packet. */
    cmd_pkt[0] = FLASH_WRITE_STATUS_CFG;
    cmd_pkt[1] = 0;

    if(srwd_block_write_prot_en)
    {
        /* When write protect to be enabled perform the following in
         * the Status Register
         *
         * SRWD[7] = 1
         * BP[3:0]: bit[5:2] = b1111
         */
        cmd_pkt[1] |= (SPI_EEPROM_STAT_REG_WR_DISABLE |
                SPI_EEPROM_PROT_ALL_BLOCKS);
    }

    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, WR_STATUS_DATA_LEN);
}

/*******************************************************************************
 * Function Name: spi_eeprom_write_enable
 *******************************************************************************
 *
 * Summary:
 *  Enable/disable write access to EEPROM. This is software based write enable.
 *  When called with enable param as "true", it sets the Write Enable Latch (WEL)
 *  bit of the EEPROM Status Register. It must be so called to enable write, program and
 *  erase commands.
 *  When called with enable param as "false", it clears the Write Enable Latch (WEL)
 *  bit of the EEPROM Status Register.
 *
 *  Note: WEL bit is cleared automatcially after successful erase or write or program operation.
 *
 * Parameters:
 *  (bool) enable Indicates write enable/disable.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_write_enable(bool enable)
{
    if (enable)
    {
        /* Create WRITE_ENABLE command packet. */
        cmd_pkt[0] = FLASH_WRITE_ENABLE;
    }
    else
    {
        /* Create WRITE_DISABLE command packet. */
        cmd_pkt[0] = FLASH_WRITE_DISABLE;
    }

    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_read_flash
 *******************************************************************************
 *
 * Summary:
 *  Read data from SPI EEPROM.
 *
 * Parameters:
 *  buffer Buffer to store data.
 *  size Size of data to be read.
 *  page_addr Page address from where data is to be read.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 * 
 * Note: This function will not read across page boundary. Thus, user must issue
 *       multiple read commands in junks of EEPROM_PAGE_SIZE data.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_read_flash(uint8_t *buffer, uint16_t size, uint32_t page_addr)
{
    if(page_addr >= EEPROM_NUM_PAGES)
    {
        return STATE_INVALID_PAGE;
    }

    /* Create READ_DATA command packet. */
    uint32_t addr = page_addr * EEPROM_PAGE_SIZE;
    POPULATE_COMMAND_ADDRESS(FLASH_READ_DATA, addr)
    
    if (size > EEPROM_PAGE_SIZE)
    {
        size = EEPROM_PAGE_SIZE;
    }
    
    return spi_master_read_write_array (NULL, buffer, size, cmd_pkt, SPI_FLASH_CMD_MAX_SIZE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_write_flash
 *******************************************************************************
 *
 * Summary:
 *  Write data to SPI EEPROM.
 *
 * Parameters:
 *  buffer Buffer to store data.
 *  size Size of data to be read.
 *  page_addr Page address from where data is to be read.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 * 
 * Note: This function will not write across page boundary. Thus, user must issue
 *       multiple read commands in junks of EEPROM_PAGE_SIZE data.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_write_flash(uint8_t *buffer, uint16_t size, uint32_t page_addr)
{
    if(page_addr >= EEPROM_NUM_PAGES)
    {
        return STATE_INVALID_PAGE;
    }

    /* Create WRITE_DATA command packet. */
    uint32_t addr = page_addr * EEPROM_PAGE_SIZE;
    POPULATE_COMMAND_ADDRESS(FLASH_WRITE_DATA, addr)

    if (size > EEPROM_PAGE_SIZE)
    {
        size = EEPROM_PAGE_SIZE;
    }

    return spi_master_read_write_array(buffer, NULL, size, cmd_pkt, SPI_FLASH_CMD_MAX_SIZE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_64k_block_erase
 *******************************************************************************
 *
 * Summary:
 *  Erase 64 KB block of SPI EEPROM.
 *
 * Parameters:
 *  page_addr Address of the start of the block to be erased.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 *
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_64k_block_erase(uint32_t page_addr)
{
    if(page_addr >= EEPROM_NUM_PAGES)
    {
        return STATE_INVALID_PAGE;
    }

    /* Create 64K Block Erase command packet. */
    uint32_t addr = page_addr * EEPROM_PAGE_SIZE;
    POPULATE_COMMAND_ADDRESS(FLASH_64K_BLOCK_ERASE, addr)

    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, SPI_FLASH_CMD_MAX_SIZE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_32k_block_erase
 *******************************************************************************
 *
 * Summary:
 *  Erase 32 KB block of SPI EEPROM.
 *
 * Parameters:
 *  page_addr Address of the start of the block to be erased.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 * 
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_32k_block_erase(uint32_t page_addr)
{
    if(page_addr >= EEPROM_NUM_PAGES)
    {
        return STATE_INVALID_PAGE;
    }

    /* Create 32K Block Erase command packet. */
    uint32_t addr = page_addr * EEPROM_PAGE_SIZE;
    POPULATE_COMMAND_ADDRESS(FLASH_32K_BLOCK_ERASE, addr)
 
    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, SPI_FLASH_CMD_MAX_SIZE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_4k_sector_erase
 *******************************************************************************
 *
 * Summary:
 *  Erase 4 KB sector of SPI EEPROM.
 *
 * Parameters:
 *  page_addr Address of the start of the block to be erased.
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 * 
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_4k_sector_erase(uint32_t page_addr)
{
    if(page_addr >= EEPROM_NUM_PAGES)
    {
        return STATE_INVALID_PAGE;
    }
    
    /* Create 4K Block Erase command packet. */
    uint32_t addr = page_addr * EEPROM_PAGE_SIZE;
    POPULATE_COMMAND_ADDRESS(FLASH_4K_SECTOR_ERASE, addr)

    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, SPI_FLASH_CMD_MAX_SIZE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_chip_erase
 *******************************************************************************
 *
 * Summary:
 *  Erase entire chip of SPI EEPROM.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  (eeprom_dma_status_t) The status of the transfer-ignition.
 * 
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_chip_erase(void)
{
    /* Create Chip Erase command packet. */
    cmd_pkt[0] = FLASH_CHIP_ERASE;

    return spi_master_read_write_array(NULL, NULL, 0, cmd_pkt, CMD_LEN_1BYTE);
}

/*******************************************************************************
 * Function Name: spi_eeprom_done
 *******************************************************************************
 *
 * Summary:
 *  To check if the transfer is done
 *
 * Parameters:
 *  None
 *
 * Return:
 *  (bool) True if SPI and DMA is idle or errors occured, false otherwise.
 * 
 ******************************************************************************/
bool spi_eeprom_done()
{
    return (dma_state_done() & Cy_SCB_SPI_IsTxComplete(FLASH_SPI_HW)) | dma_has_error();
}

/*******************************************************************************
 * Function Name: spi_transfer_get_error
 *******************************************************************************
 *
 * Summary:
 *  Errors related to SPI.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  (cy_rslt_t) Error from SCB | DMA.
 * 
 ******************************************************************************/
cy_rslt_t spi_transfer_get_error()
{
    return (Cy_SCB_SPI_GetSlaveMasterStatus(FLASH_SPI_HW) & (~CY_SCB_SPI_MASTER_DONE)) | dma_get_error();
}

/*******************************************************************************
 * Function Name: spi_state_reset
 *******************************************************************************
 *
 * Summary:
 *  Reset everything related to SPI and DMA.
 *
 * Parameters:
 *  None
 *
 * Return:
 *  (uint32_t) Returns INIT_SUCCESS.
 * 
 ******************************************************************************/
void spi_state_reset(void)
{
    Cy_SCB_SPI_ClearRxFifo(FLASH_SPI_HW);
    Cy_SCB_SPI_ClearTxFifo(FLASH_SPI_HW);
    dma_state_reset();
}

/*******************************************************************************
 * Function Name: spi_master_read_write_array
 *******************************************************************************
 *
 * Summary:
 *  Internal function to start the transfer. Will call appropriate DMA related
 *  functions. First send is command. Second send is user data.
 *
 * Parameters:
 *  wr_buf: Pointer to the user buffer to write to.
 *  rd_buf: Pointer to the user buffer to read from.
 *  size: Number of bytes for user operation.
 *  cmd_buf: Pointer to the command buffer.
 *  cmd_size: Number of bytes for command operation.
 * 
 *  bg_status: Global variable with backup of status. This is set to force
 *  re-checking status after transfer completion.
 *
 * Return:
 *  (uint32_t) Returns STATE_UNCONFIRMED_SUCCESS if transfer was started.
 *  Returns STATE_INVALID_COMMAND if cmd_buf is NULL or size is 0.
 * 
 ******************************************************************************/
eeprom_dma_status_t spi_master_read_write_array(uint8_t *wr_buf, uint8_t *rd_buf,
        uint16_t size, uint8_t *cmd_buf, uint8_t cmd_size)
{
    if (cmd_buf == NULL || cmd_size == 0)
    {
        return STATE_INVALID_COMMAND;
    }

    /* Preset variable so that after actual command completes,
     * the interrupt will trigger another Read command 
     * and further wait until WIP-bit is cleared */
    if (!(cmd_buf[0] == FLASH_READ_DATA || cmd_buf[0] == FLASH_READ_STATUS ||
            cmd_buf[0] == FLASH_READ_STATUS_2 || cmd_buf[0] == FLASH_READ_CONFIG ||
            cmd_buf[0] == FLASH_RDID))
    {
        bg_status.status |= SPI_EEPROM_STAT_REG_WIP;
    }

    if (size == 0)
    {
        pong = (dma_master_packet_t)
        {
            .src = cmd_buf, 
            .dst = NULL,
            .num_bytes = cmd_size
        };
        send_packet(&pong);
    }
    else
    {
        ping = (dma_master_packet_t)
        {
            .src = cmd_buf, 
            .dst = NULL,
            .num_bytes = cmd_size
        };
        pong = (dma_master_packet_t)
        {
            .src = wr_buf, 
            .dst = rd_buf,
            .num_bytes = size
        };
        send_packet_multi(&ping, &pong);
    }
    return STATE_UNCONFIRMED_SUCCESS;
}

/* [] END OF FILE */
