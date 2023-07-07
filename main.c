/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the SPI-master via DMA interfacing
*              with EEPROM Example for ModusToolbox.
*
* Related Document: See README.md
*
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
#include "cy_pdl.h"
#include "cycfg.h"
#include "cybsp.h"
#include "spi_eeprom_master.h"
#include <stdio.h>

/*******************************************************************************
* Macros
********************************************************************************/
/* The number of bytes read/written from/to the EEPROM must be less then
 * EEPROM_PAGE_SIZE (256) */
#define DATA_SIZE           (200u)

/* Data Page */
#define DATA_PAGE           (0u)

/* Number of times to try to write status register of EEPROM */
#define RETRY_COUNT         (3u)

/* Debug print macro to enable the UART print */
#define DEBUG_PRINT         (1u)

/* Delay for User LED */
#define LED_DELAY_MS        (500)

/* CY ASSERT failure */
#define CY_ASSERT_FAILED    (0U)

/*******************************************************************************
* Global Variables
*******************************************************************************/
#if DEBUG_PRINT
/* Variable used for tracking the print status */
volatile bool ENTER_LOOP = true;

cy_stc_scb_uart_context_t CYBSP_UART_context;

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Prints the error message.
*
* Parameters:
*  error_msg - message to print if any error encountered.
*  status - status obtained after evaluation.
*
* Return:
*  void
*
*******************************************************************************/
void check_status(char *message, cy_rslt_t status)
{
    char error_msg[50];

    sprintf(error_msg, "Error Code: 0x%08X\n", (unsigned int) status);

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\nFAIL: ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, message);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, error_msg);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
}
#endif

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  System entrance point. This function performs
*  - initial setup of device
*  - call EEPROM library to:
*    - configure the SCB block as SPI interface
*    - configure the DMA block to transfer data to SPI-FIFO
*  - write data to EEPROM, read data back
*  - after successful read: blinks an LED under firmware control at 1 Hz
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    /* Variable initialization */
    cy_rslt_t result;
    eeprom_dma_status_t eeprom_result;

    /* Variables to check write access */
    uint8_t EEPROM_status_counter = 0;
    uint8_t EEPROM_status = 0xFF;

    /* Array to store data which needs to be written in EEPROM */
    uint8_t writeData[DATA_SIZE];

    /* Array to read data from EEPROM */
    uint8_t readData[DATA_SIZE];

    uint8_t data = 0;

    /* Initializing the read data array to zero */
    memset(readData, 0, DATA_SIZE);

#if DEBUG_PRINT
    /* String for UART print */
    char strArray[4];
#endif

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

#if DEBUG_PRINT
    /* Configure and enable the UART peripheral */
    Cy_SCB_UART_Init(CYBSP_UART_HW, &CYBSP_UART_config, &CYBSP_UART_context);
    Cy_SCB_UART_Enable(CYBSP_UART_HW);

    /* Send a string over serial terminal */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\x1b[2J\x1b[;H");

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "**************************");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "PMG1 MCU: SPI Flash");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "************************** \r\n\n");
#endif

    for(int i = 0; i < DATA_SIZE; ++i)
    {
        if(data == 0)
        {
            data = 1;
        }

        writeData[i] = data;
        data <<= 1;            /* Create 1-2-4-8-10-20-40-80-Pattern */
    }

    /* Initialize the SPI and DMA as part of EEPROM driver interface*/
    eeprom_result = spi_eeprom_init();
    if (eeprom_result != INIT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API spi_eeprom_init failed with error code", eeprom_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* See if we have write access by reading BP1/BP0 */
    do
    {
        EEPROM_status_counter++;

        spi_eeprom_write_status_reg(false);
        while(!spi_eeprom_done());

        spi_eeprom_read_status_reg(&EEPROM_status);
        while(!spi_eeprom_done());
    } while((EEPROM_status_counter < RETRY_COUNT) && (EEPROM_status & SPI_EEPROM_PROT_ALL_BLOCKS));

    if(EEPROM_status_counter == RETRY_COUNT)
    {
#if DEBUG_PRINT
        check_status("API spi_eeprom_write_status_reg failed with error code", spi_transfer_get_error());
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable write enable */
    spi_eeprom_write_enable(true);

    /* Wait for all operations to finish. */
    while(!spi_eeprom_done());

    /* Sector Erase */
    spi_eeprom_4k_sector_erase(DATA_PAGE);

    /* Wait for all operations to finish. */
    while(!spi_eeprom_done());

    if(spi_transfer_get_error())
    {
#if DEBUG_PRINT
        check_status("API spi_eeprom_4k_sector_erase failed with error code", spi_transfer_get_error());
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable write enable */
    spi_eeprom_write_enable(true);

    /* Wait for all operations to finish. */
    while(!spi_eeprom_done());

    /* Write data to EEPROM */
    eeprom_result = spi_eeprom_write_flash(writeData, DATA_SIZE, DATA_PAGE);
    if(eeprom_result != STATE_UNCONFIRMED_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API spi_eeprom_write_flash failed with error code", eeprom_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }
    else
    {
#if DEBUG_PRINT
        Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\nWrite Success\r\n");
#endif
    }

    /* Wait for all operations to finish. */
    while(!spi_eeprom_done());      

    /* Read data from EEPROM */
    eeprom_result = spi_eeprom_read_flash(readData, DATA_SIZE, DATA_PAGE);
    if(eeprom_result != STATE_UNCONFIRMED_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API spi_eeprom_read_flash failed with error code", eeprom_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }
    else
    {
#if DEBUG_PRINT
        Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\nRead Success\r\n");
#endif
    }

    /* Wait for all operations to finish. */
    while(!spi_eeprom_done());      

#if DEBUG_PRINT
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\nData read: \r\n");

    /* Print the data read from EEPROM */
    for(int i = 0; i < DATA_SIZE; ++i)
    {
        sprintf(strArray, "%02X ", readData[i]);
        Cy_SCB_UART_PutString(CYBSP_UART_HW, strArray);
    }

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n");
#endif

    /* Compare the data written and data read to/from EEPROM */
    result = memcmp(writeData, readData, DATA_SIZE);
    if(result != 0)
    {
#if DEBUG_PRINT
        Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\nData mismatch\r\n");
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }
    else
    {
#if DEBUG_PRINT
        Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\nData matched\r\n");
#endif
    }

    /* Blink otherwise */
    for (;;)
    {
        /* Toggle the user LED state */
        Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);

        /* Wait for 0.5 seconds */
        Cy_SysLib_Delay(LED_DELAY_MS);

#if DEBUG_PRINT
        if (ENTER_LOOP)
        {
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Entered for loop\r\n");
            ENTER_LOOP = false;
        }
#endif
    }
}

/* [] END OF FILE */
