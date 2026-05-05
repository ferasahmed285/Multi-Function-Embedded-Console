#include "adc.h"
#include "uart.h"

/* ================= REGISTERS ================= */
#define SYSCTL_RCGCADC_R   (*((volatile unsigned long *)0x400FE638))
#define SYSCTL_RCGCTIMER_R (*((volatile unsigned long *)0x400FE604))

#define ADC0_ACTSS_R   (*((volatile unsigned long *)0x40038000))
#define ADC0_EMUX_R    (*((volatile unsigned long *)0x40038014))
#define ADC0_PSSI_R    (*((volatile unsigned long *)0x40038028))
#define ADC0_RIS_R     (*((volatile unsigned long *)0x40038004))
#define ADC0_ISC_R     (*((volatile unsigned long *)0x4003800C))
#define ADC0_SSFIFO3_R (*((volatile unsigned long *)0x400380A8))
#define ADC0_SSMUX3_R  (*((volatile unsigned long *)0x400380A0))
#define ADC0_SSCTL3_R  (*((volatile unsigned long *)0x400380A4))
#define ADC0_IM_R      (*((volatile unsigned long *)0x40038008))

#define NVIC_EN0_R     (*((volatile unsigned long *)0xE000E100))

/* TIMER0 */
#define TIMER0_CFG_R   (*((volatile unsigned long *)0x40030000))
#define TIMER0_TAMR_R  (*((volatile unsigned long *)0x40030004))
#define TIMER0_TAILR_R (*((volatile unsigned long *)0x40030028))
#define TIMER0_CTL_R   (*((volatile unsigned long *)0x4003000C))
#define TIMER0_IMR_R   (*((volatile unsigned long *)0x40030018))
#define TIMER0_ICR_R   (*((volatile unsigned long *)0x40030024))

static float threshold = 30.0;

/* ================= TEMP CONVERT ================= */
static float ConvertTemp(int adc)
{
    return 147.5 - ((75.0 * 3.3 * adc) / 4096.0);
}

/* ================= INIT ADC ================= */
void ADC0_Init(void)
{
    SYSCTL_RCGCADC_R |= 1;
    SYSCTL_RCGCTIMER_R |= 1;

    for(int i=0;i<1000;i++);

    /* disable SS3 */
    ADC0_ACTSS_R &= ~(1 << 3);

    /* select channel 0 (temp sensor) */
    ADC0_SSMUX3_R = 0;

    /* IE0 + END0 + TS0 */
    ADC0_SSCTL3_R = (1<<1) | (1<<2) | (1<<3);

    /* enable ADC interrupt */
    ADC0_IM_R |= (1 << 3);

    /* enable NVIC for ADC */
    NVIC_EN0_R |= (1 << 17);

    /* enable SS3 */
    ADC0_ACTSS_R |= (1 << 3);
}

/* ================= TIMER + ADC TRIGGER ================= */
void ADC0_EnableTimerTrigger(void)
{
    TIMER0_CTL_R = 0;

    TIMER0_CFG_R = 0;
    TIMER0_TAMR_R = 0x02;

    TIMER0_TAILR_R = 16000000 - 1;

    TIMER0_ICR_R = 1;
    TIMER0_IMR_R = 1;

    NVIC_EN0_R |= (1 << 19);

    
    TIMER0_CTL_R |= (1 << 5) | 1;

    /* THEN CONFIGURE ADC TRIGGER */
    ADC0_EMUX_R &= ~(0xF << 12);
    ADC0_EMUX_R |=  (0x5 << 12);
}

void ADC0_SetThreshold(float t)
{
    threshold = t;
}

/* ================= ADC ISR ================= */
void ADC0SS3_Handler(void)
{
    int adc = ADC0_SSFIFO3_R & 0xFFF;

    ADC0_ISC_R = (1 << 3);

    float temp = ConvertTemp(adc);

    UART0_WriteString("Temp: ");

    int ip = (int)temp;
    int dp = (int)((temp - ip) * 10);

    char buf[20];
    char *p = buf;

    if(ip == 0) *p++ = '0';
    else
    {
        int rev[10], i=0;
        while(ip)
        {
            rev[i++] = (ip % 10) + '0';
            ip /= 10;
        }
        while(i--) *p++ = rev[i];
    }

    *p++ = '.';
    *p++ = dp + '0';
    *p++ = ' ';
    *p++ = 'C';
    *p++ = '\r';
    *p++ = '\n';
    *p = 0;

    UART0_WriteString(buf);

    if(temp > threshold)
    {
        UART0_WriteString("WARNING HIGH TEMP!\r\n");
    }
}

/* TIMER ISR */
void TIMER0A_Handler(void)
{
    TIMER0_ICR_R = 1;
}