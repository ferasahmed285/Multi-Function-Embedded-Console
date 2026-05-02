/******************************************************************************
 * File: GPTM.c
 * Module: General-Purpose Timer Module
 * Description: Source file for TM4C123GH6PM Timer Driver
 ******************************************************************************/

#include "GPTM.h"
#include "TM4C123GH6PM.h" // Device-specific header for CMSIS register definitions

/*
 * Timer1A_Init
 * Configures Timer 1A to tick exactly once per second.
 */
void Timer1A_Init(void)
{
    /* 1. Enable the clock for Timer 1 */
    SYSCTL->RCGCTIMER |= (1U << 1);  

    /* Wait until Timer 1 peripheral is ready */
    while ((SYSCTL->PRTIMER & (1U << 1)) == 0);

    /* 2. Disable Timer 1A before making configuration changes */
    TIMER1->CTL &= ~(1U << 0);       

    /* 3. Configure as a 32-bit timer */
    TIMER1->CFG = 0x00;              

    /* 4. Configure for Periodic mode */	
    TIMER1->TAMR = 0x02;             

    /* 5. Load the start value 
     *    System clock is 16 MHz. To get a 1-second delay:
     *    16,000,000 ticks - 1 = 15,999,999
     */
    TIMER1->TAILR = 16000000 - 1;    

    /* 6. Clear any pending timeout interrupts */
    TIMER1->ICR = 0x01;              

    /* 7. Enable the timeout interrupt mask */
    TIMER1->IMR |= 0x01;             

    /* 8. Enable the Timer 1A interrupt in the NVIC */
    NVIC_EnableIRQ(TIMER1A_IRQn);

    /* 9. Re-enable Timer 1A */
    TIMER1->CTL |= (1U << 0);        
}