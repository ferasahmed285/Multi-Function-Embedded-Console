#include "uart.h"

/* Hardware Register Addresses (Bare-Metal) */
#define SYSCTL_RCGCUART_R  (*((volatile unsigned long *)0x400FE618))
#define SYSCTL_RCGCGPIO_R  (*((volatile unsigned long *)0x400FE608))

#define GPIO_PORTA_AFSEL_R (*((volatile unsigned long *)0x40004420))
#define GPIO_PORTA_PCTL_R  (*((volatile unsigned long *)0x4000452C))
#define GPIO_PORTA_DEN_R   (*((volatile unsigned long *)0x4000451C))
#define GPIO_PORTA_AMSEL_R (*((volatile unsigned long *)0x40004528))

#define UART0_DR_R         (*((volatile unsigned long *)0x4000C000))
#define UART0_FR_R         (*((volatile unsigned long *)0x4000C018))
#define UART0_IBRD_R       (*((volatile unsigned long *)0x4000C024))
#define UART0_FBRD_R       (*((volatile unsigned long *)0x4000C028))
#define UART0_LCRH_R       (*((volatile unsigned long *)0x4000C02C))
#define UART0_CTL_R        (*((volatile unsigned long *)0x4000C030))
#define UART0_IM_R         (*((volatile unsigned long *)0x4000C038)) 
#define UART0_CC_R         (*((volatile unsigned long *)0x4000CFC8))

#define NVIC_EN0_R         (*((volatile unsigned long *)0xE000E100))

void UART0_Init(void)
{
    SYSCTL_RCGCUART_R |= 0x01;
    SYSCTL_RCGCGPIO_R |= 0x01;
    while((SYSCTL_RCGCGPIO_R & 0x01) == 0); 

    GPIO_PORTA_AFSEL_R |= 0x03;
    GPIO_PORTA_PCTL_R   = (GPIO_PORTA_PCTL_R & 0xFFFFFF00) | 0x00000011;
    GPIO_PORTA_DEN_R   |= 0x03;
    GPIO_PORTA_AMSEL_R &= ~0x03;

    UART0_CTL_R &= ~0x01;

    /* REVERTED TO 16 MHz CONFIG (9600 Baud) */
    UART0_IBRD_R = 104;
    UART0_FBRD_R = 11;

    UART0_LCRH_R = 0x60; // 8-bit, FIFO Disabled
    UART0_CC_R = 0x0;

    UART0_IM_R |= 0x10;  // Enable RX Interrupt
    NVIC_EN0_R |= (1 << 5);

    UART0_CTL_R = 0x301;
}

void UART0_WriteChar(char c)
{
    while(UART0_FR_R & (1 << 5)); 
    UART0_DR_R = c;
}

void UART0_WriteString(const char *str)
{
    while(*str)
    {
        UART0_WriteChar(*str++);
    }
}