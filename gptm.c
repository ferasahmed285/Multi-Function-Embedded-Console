

#include "GPTM.h"
#include "TM4C123GH6PM.h" 



 /* Configures Timer 1A to tick exactly once per second. */
 
void Timer1A_Init(void)
{
    SYSCTL->RCGCTIMER |= (1U << 1);  

    while ((SYSCTL->PRTIMER & (1U << 1)) == 0);

    TIMER1->CTL &= ~(1U << 0);       

    TIMER1->CFG = 0x00;              
	
    TIMER1->TAMR = 0x02;             

    TIMER1->TAILR = 16000000 - 1;    

    TIMER1->ICR = 0x01;              

    TIMER1->IMR |= 0x01;             

    NVIC_EnableIRQ(TIMER1A_IRQn);

    TIMER1->CTL |= (1U << 0);        
}