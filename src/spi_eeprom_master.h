/******************************************************************************
 * File Name: spi_eeprom_master.h
 *
 * Description: Header file for SPI master interface to SPI EEPROM slave.
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

#ifndef _SPI_EEPROM_MASTER_H_
#define _SPI_EEPROM_MASTER_H_

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include <stdint.h>
#include "cy_pdl.h"
#include "cycfg.h"
#include "status.h"

/*******************************************************************************
* Macros
********************************************************************************/
/* SPI EEPROM Status Register (Write In Progress bit) */
#define SPI_EEPROM_STAT_REG_WIP                 (1 << 0)

/* SPI EEPROM Status Register (Write Enable latch bit) */
#define SPI_EEPROM_STAT_REG_WEL                 (1 << 1)

/* SPI EEPROM Status Register (Write disable bit) */
#define SPI_EEPROM_STAT_REG_WR_DISABLE          ((1 << 7))

/* Macro which enables protection to all blocks of the SPI EEPROM */
#define SPI_EEPROM_PROT_ALL_BLOCKS              (0xF << 2)

#define SPI_EEPROM_IS_WRITE_IN_PROGRESS(status) (((status) & SPI_EEPROM_STAT_REG_WIP) != 0)

#define SPI_EEPROM_IS_WRITE_ENABLED(status)     (((status) & SPI_EEPROM_STAT_REG_WEL) != 0)

/* Command Length */
#define CMD_LEN_1BYTE                           (1u)

/* Read Status Data Length */
#define RD_STATUS_DATA_LEN                      (1u)

/* Write Status Data Length */
#define WR_STATUS_DATA_LEN                      (2u)

/* Read Status Singular Length */
#define RD_STATUS_SINGULAR_LEN                  (2u)

/* The below macros can be changed according to the EEPROM used */
/* Size of EEPROM Page */
#define EEPROM_PAGE_SIZE                        (256u)

/* Number of pages in EEPROM */
#define EEPROM_NUM_PAGES                        (32768u)

/* EEPROM Address Types (8-bit, 16-bit, 24-bit, 32-bit) */
#define EEPROM_ADDRESS_TYPE_8                   (1)
#define EEPROM_ADDRESS_TYPE_16                  (2)
#define EEPROM_ADDRESS_TYPE_24                  (3)
#define EEPROM_ADDRESS_TYPE_32                  (4)

/* Set the EEPROM address type as per the EEPROM device used */
#define SET_EEPROM_ADDRESS_TYPE                 (EEPROM_ADDRESS_TYPE_24)

/******************************************************************************
 * Structure/Enum type declaration
 ******************************************************************************/
/* Structure to hold SPI EEPROM instruction IDs */
typedef enum spi_flash_cmd
{
    FLASH_WRITE_STATUS_CFG = 1,
    FLASH_WRITE_DATA = 2,
    FLASH_READ_DATA = 3,
    FLASH_WRITE_DISABLE = 4,
    FLASH_READ_STATUS = 5,
    FLASH_WRITE_ENABLE = 6,
    FLASH_READ_STATUS_2 = 7,
    FLASH_4K_SECTOR_ERASE = 0x20,
    FLASH_READ_CONFIG = 0x35,
    FLASH_32K_BLOCK_ERASE = 0x52,
    FLASH_64K_BLOCK_ERASE = 0xD8,
    FLASH_CHIP_ERASE = 0x60,
    FLASH_CHIP_ERASE_ALT = 0xC7,
    FLASH_RDID = 0x9F
} spi_flash_cmd_t;

/******************************************************************************
 * Global function declaration
 ******************************************************************************/
eeprom_dma_status_t spi_eeprom_init(void);
eeprom_dma_status_t spi_eeprom_rdid_reg(uint8_t *data, uint8_t data_len);
eeprom_dma_status_t spi_eeprom_read_status_reg(uint8_t *status);
eeprom_dma_status_t spi_eeprom_read_status_2_reg(uint8_t *status);
eeprom_dma_status_t spi_eeprom_read_config_reg(uint8_t *rd_config);
eeprom_dma_status_t spi_eeprom_write_status_reg(bool srwd_block_write_prot_en);
eeprom_dma_status_t spi_eeprom_write_enable(bool enable);
eeprom_dma_status_t spi_eeprom_read_flash(uint8_t *buffer, uint16_t size, uint32_t page_addr);
eeprom_dma_status_t spi_eeprom_write_flash(uint8_t *buffer, uint16_t size, uint32_t page_addr);
eeprom_dma_status_t spi_eeprom_64k_block_erase(uint32_t page_addr);
eeprom_dma_status_t spi_eeprom_32k_block_erase(uint32_t page_addr);
eeprom_dma_status_t spi_eeprom_4k_sector_erase(uint32_t page_addr);
eeprom_dma_status_t spi_eeprom_chip_erase(void);

bool spi_eeprom_done(void);
cy_rslt_t spi_transfer_get_error(void);
void spi_state_reset(void);

eeprom_dma_status_t spi_master_read_write_array(uint8_t *wr_buf, uint8_t *rd_buf,
        uint16_t size, uint8_t *cmd_buff, uint8_t cmd_size);
bool dma_completion_cb(void);

#endif /* _SPI_EEPROM_MASTER_H_ */
