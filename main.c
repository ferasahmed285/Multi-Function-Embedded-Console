#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "uart.h"
#include "gptm.h"
#include "adc.h"

/* ==========================================================================
 * Bare-Metal Hardware Registers
 * ========================================================================== */
#define UART0_DR_R     (*((volatile unsigned long *)0x4000C000))
#define UART0_MIS_R    (*((volatile unsigned long *)0x4000C040))
#define UART0_ICR_R    (*((volatile unsigned long *)0x4000C044))
#define CPACR_R        (*((volatile unsigned long *)0xE000ED88))

/* Port F Registers for the Onboard RGB LEDs */
#define SYSCTL_RCGCGPIO_R  (*((volatile unsigned long *)0x400FE608))
#define GPIO_PORTF_DATA_R  (*((volatile unsigned long *)0x400253FC))
#define GPIO_PORTF_DIR_R   (*((volatile unsigned long *)0x40025400))
#define GPIO_PORTF_DEN_R   (*((volatile unsigned long *)0x4002551C))

/* Timer/ADC Registers used to control background interrupts */
#define TIMER0_CTL_R   (*((volatile unsigned long *)0x4003000C)) 
#define TIMER1_ICR_R   (*((volatile uint32_t *)0x40031024))      
#define ADC0_RIS_R     (*((volatile unsigned long *)0x40038004))
#define ADC0_ISC_R     (*((volatile unsigned long *)0x4003800C))
#define ADC0_SSFIFO3_R (*((volatile unsigned long *)0x400380A8))
#define NVIC_DIS0_R    (*((volatile unsigned long *)0xE000E180)) 

/* SysTick Registers (Task 2) */
#define NVIC_ST_CTRL_R      (*((volatile unsigned long *)0xE000E010))
#define NVIC_ST_RELOAD_R    (*((volatile unsigned long *)0xE000E014))
#define NVIC_ST_CURRENT_R   (*((volatile unsigned long *)0xE000E018))

#define BUFFER_SIZE 64

/* ==========================================================================
 * Application States & Globals
 * ========================================================================== */
typedef enum {
    MODE_MENU,
    MODE_CALC,
    MODE_TIMER,
    MODE_STOPWATCH,
    MODE_TEMP
} AppMode;

volatile AppMode current_mode = MODE_MENU;

/* Calculator Globals */
volatile char rx_buffer[BUFFER_SIZE];
volatile uint8_t rx_index = 0;
volatile bool calc_ready = false;

/* Stopwatch Globals */
volatile uint8_t stopwatch_running = 0;
volatile uint32_t minutes = 0;
volatile uint32_t seconds = 0;

/* Temp Monitor Globals */
volatile float temp_threshold = 30.0;
volatile bool temp_warning = false;

/* SysTick Timer Globals (Task 2) */
volatile uint32_t timer_total_seconds = 0;
volatile uint8_t timer_running = 0;
volatile uint8_t timer_paused = 0;
volatile uint8_t timer_input_idx = 0;
volatile char timer_input_buf[5];
volatile bool timer_alarm = false;

/* ==========================================================================
 * Helper Function Prototypes
 * ========================================================================== */
static void float_to_string(float n, char* res, int decimals);
static float string_to_float(const volatile char *str, int *bytes_read);
static void DelayMs(uint32_t ms);
static void LEDs_Init(void);
static void Blink_Error_LED(void);
static void Blink_Timer_LED(void);
void UART0_Send2Digits(uint32_t num);
void UART0_PrintTime(void);
static float ConvertTemp(int adc);
void PrintMenu(void);
void SysTick_Init(void);

/* ==========================================================================
 * MAIN LOOP
 * ========================================================================== */
int main(void)
{
    int i;
    
    CPACR_R |= 0x00F00000;

    UART0_Init();
    LEDs_Init();
    Timer1A_Init();
    SysTick_Init();
    
    ADC0_Init();
    
    /* Disable the ADC0SS3 interrupt in the NVIC so the handler inside 
       the read-only adc.c file is suppressed. We will poll the ADC instead. */
    NVIC_DIS0_R = (1 << 17); 
    
    __asm("cpsie i");

    PrintMenu();

    while(1)
    {
        if (current_mode == MODE_CALC)
        {
            if (calc_ready == true)
            {
                float num1 = 0, num2 = 0, result = 0;
                char op = 0;
                char print_str[20];
                int idx = 0, len = 0;
                bool success = true;

                num1 = string_to_float(&rx_buffer[idx], &len);
                idx += len;
                while (rx_buffer[idx] == ' ') idx++;
                op = rx_buffer[idx++];
                while (rx_buffer[idx] == ' ') idx++;
                num2 = string_to_float(&rx_buffer[idx], &len);

                if (op == '+')      result = num1 + num2;
                else if (op == '-') result = num1 - num2;
                else if (op == '*') result = num1 * num2;
                else if (op == '/') {
                    if (num2 == 0) {
                        success = false;
                        UART0_WriteString("\r\n[ERROR] Division by zero!\r\n");
                        Blink_Error_LED();
                    } else result = num1 / num2;
                }
                else success = false;

                if (success) {
                    UART0_WriteString("\r\n");
                    float_to_string(num1, print_str, 3);
                    UART0_WriteString(print_str);
                    UART0_WriteChar(' '); UART0_WriteChar(op); UART0_WriteChar(' ');
                    float_to_string(num2, print_str, 3);
                    UART0_WriteString(print_str);
                    UART0_WriteString(" = ");
                    float_to_string(result, print_str, 3);
                    UART0_WriteString(print_str);
                    UART0_WriteString("\r\n");
                }

                rx_index = 0;
                for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
                calc_ready = false;
                UART0_WriteString("> ");
            }
        }
        else if (current_mode == MODE_TIMER)
        {
            if (timer_alarm == true) 
            {
                timer_alarm = false;
                UART0_WriteString("\r\n\n[!] TIMER FINISHED! [!]\r\n");
                Blink_Timer_LED(); // Blinks the Blue LED
                
                /* Reset input state for a new timer */
                timer_input_idx = 0;
                UART0_WriteString("\r\nEnter MMSS: ");
            }
            else {
                __asm("WFI");
            }
        }
        else if (current_mode == MODE_TEMP)
        {
            if (ADC0_RIS_R & (1 << 3))
            {
                int adc = ADC0_SSFIFO3_R & 0xFFF;
                ADC0_ISC_R = (1 << 3); 

                float temp = ConvertTemp(adc);
                UART0_WriteString("Temp: ");

                int ip = (int)temp;
                int dp = (int)((temp - ip) * 10);
                if(dp < 0) dp = -dp; 

                char buf[20]; char *p = buf;

                if(ip == 0) *p++ = '0';
                else {
                    int rev[10], k=0;
                    while(ip) { rev[k++] = (ip % 10) + '0'; ip /= 10; }
                    while(k--) *p++ = rev[k];
                }

                *p++ = '.'; *p++ = dp + '0'; *p++ = ' '; *p++ = 'C'; *p++ = '\r'; *p++ = '\n'; *p = 0;
                UART0_WriteString(buf);

                if(temp > temp_threshold) { 
                    UART0_WriteString("\r\n[!] WARNING: HIGH TEMP EXCEEDS THRESHOLD!\r\n\n"); 
                    Blink_Error_LED(); // Red LED
                }
            }
            __asm("WFI");
        }
        else
        {
            __asm("WFI");
        }
    }
}

/* ==========================================================================
 * UART0 RECEIVE INTERRUPT (The Router & Escape Hatch)
 * ========================================================================== */
void UART0_Handler(void)
{
    int i;
    if(UART0_MIS_R & 0x10)
    {
        char c = (char)(UART0_DR_R & 0xFF);
        UART0_ICR_R = 0x10;

        /* ================= THE GLOBAL ESCAPE HATCH ================= */
        if (c == 'q' || c == 'Q') 
        {
            stopwatch_running = 0; 
            timer_running = 0;
            TIMER0_CTL_R &= ~0x01; 
            
            calc_ready = false;
            temp_warning = false;
            timer_alarm = false;
            rx_index = 0;
            for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
            
            current_mode = MODE_MENU;
            PrintMenu();
            return; 
        }

        switch (current_mode) 
        {
            case MODE_MENU:
                if (c == '1') {
                    current_mode = MODE_CALC;
                    UART0_WriteString("\033[2J\033[H--- Calculator ---\r\nType equation (e.g. 5.5 * 2.2 =)\r\nPress 'C' to clear, 'Q' to quit.\r\n> ");
                    rx_index = 0;
                    for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
                } else if (c == '2') {
                    current_mode = MODE_TIMER;
                    UART0_WriteString("\033[2J\033[H--- Timer Mode ---\r\nPress 'Q' to quit.\r\nEnter MMSS: ");
                    timer_input_idx = 0;
                    timer_running = 0;
                } else if (c == '3') {
                    current_mode = MODE_STOPWATCH;
                    UART0_WriteString("\033[2J\033[H--- Stopwatch Mode ---\r\nS = Start | P = Pause | R = Reset | Q = Quit\r\n");
                    UART0_PrintTime();
                } else if (c == '4') {
                    current_mode = MODE_TEMP;
                    UART0_WriteString("\033[2J\033[H--- Temp Monitor ---\r\n");
                    UART0_WriteString("Current Threshold: ");
                    char print_str[20];
                    float_to_string(temp_threshold, print_str, 2);
                    UART0_WriteString(print_str);
                    UART0_WriteString(" C\r\n");
                    UART0_WriteString("Type new threshold and press '=' to set.\r\nMonitoring temperature (Press 'Q' to quit)...\r\n\n");
                    
                    rx_index = 0;
                    for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
                    ADC0_EnableTimerTrigger();
                }
                break;

            case MODE_CALC:
                if (calc_ready) return;

                if (c == 'c' || c == 'C') {
                    rx_index = 0;
                    for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
                    UART0_WriteString("\r\n[Cleared]\r\n> ");
                }
                else if (c == '=' || c == '\r') {
                    UART0_WriteChar('=');
                    rx_buffer[rx_index] = '\0';
                    calc_ready = true;
                }
                else if (c == '\b' || c == 127) {
                    if (rx_index > 0) { rx_index--; UART0_WriteString("\b \b"); }
                }
                else {
                    if (rx_index < (BUFFER_SIZE - 1)) {
                        rx_buffer[rx_index++] = c;
                        UART0_WriteChar(c);
                    }
                }
                break;

            case MODE_TIMER:
                if (timer_input_idx < 4) {
                    if (c >= '0' && c <= '9') {
                        timer_input_buf[timer_input_idx++] = c;
                        UART0_WriteChar(c);
                        
                        if (timer_input_idx == 4) {
                            uint32_t mins = (timer_input_buf[0]-'0')*10 + (timer_input_buf[1]-'0');
                            uint32_t secs = (timer_input_buf[2]-'0')*10 + (timer_input_buf[3]-'0');
                            timer_total_seconds = (mins * 60) + secs;
                            UART0_WriteString("\r\n>> S:Start P:Pause R:Reset\r\n");
                        }
                    }
                    else if (c == '\b' || c == 127) {
                        if (timer_input_idx > 0) { timer_input_idx--; UART0_WriteString("\b \b"); }
                    }
                } else {
                    if (c == 'S' || c == 's') {
                        timer_running = 1; timer_paused = 0;
                        UART0_WriteString("\r\n>> Started\r\n");
                    } else if (c == 'P' || c == 'p') {
                        timer_paused = 1;
                        UART0_WriteString("\r\n>> Paused\r\n");
                    } else if (c == 'R' || c == 'r') {
                        timer_running = 0; timer_paused = 0; timer_total_seconds = 0;
                        timer_input_idx = 0;
                        UART0_WriteString("\r\n>> Reset\r\nEnter MMSS: ");
                    }
                }
                break;

            case MODE_STOPWATCH:
                if (c == 'S' || c == 's') {
                    if (stopwatch_running == 0) { stopwatch_running = 1; UART0_WriteString("\r\n>> Started\r\n"); UART0_PrintTime(); }
                }
                else if (c == 'P' || c == 'p') {
                    if (stopwatch_running == 1) { stopwatch_running = 0; UART0_WriteString("\r\n>> Paused\r\n"); UART0_PrintTime(); }
                }
                else if (c == 'R' || c == 'r') {
                    stopwatch_running = 0; minutes = 0; seconds = 0;
                    UART0_WriteString("\r\n>> Reset\r\n"); UART0_PrintTime();
                }
                break;
                
            case MODE_TEMP:
                if (c == '=' || c == '\r') {
                    rx_buffer[rx_index] = '\0';
                    if (rx_index > 0) {
                        int len = 0;
                        float new_thresh = string_to_float(rx_buffer, &len);
                        temp_threshold = new_thresh;
                        
                        UART0_WriteString("\r\n>> New Threshold Set: ");
                        char print_str[20]; float_to_string(temp_threshold, print_str, 2);
                        UART0_WriteString(print_str); UART0_WriteString(" C\r\n\n");
                        
                        rx_index = 0;
                        for(i=0; i<BUFFER_SIZE; i++) rx_buffer[i] = 0;
                    }
                }
                else if (c == '\b' || c == 127) {
                    if (rx_index > 0) { rx_index--; UART0_WriteString("\b \b"); }
                }
                else {
                    if (rx_index < (BUFFER_SIZE - 1)) {
                        rx_buffer[rx_index++] = c;
                        UART0_WriteChar(c);
                    }
                }
                break;
        }
    }
}

/* ==========================================================================
 * HARDWARE INTERRUPT HANDLERS
 * ========================================================================== */

void TIMER1A_Handler(void)
{
    TIMER1_ICR_R = 0x01; 
    
    if (stopwatch_running)
    {
        seconds++;
        if (seconds >= 60) { seconds = 0; minutes++; }
        if (current_mode == MODE_STOPWATCH) UART0_PrintTime();
    }
}

void SysTick_Handler(void)
{
    if (timer_running && !timer_paused && timer_total_seconds > 0)
    {
        timer_total_seconds--;
        if (timer_total_seconds == 0)
        {
            timer_running = 0;
            timer_alarm = true; // Signal main loop to blink blue LED
        }
    }
}

/* ==========================================================================
 * UTILITY FUNCTIONS
 * ========================================================================== */
void PrintMenu(void)
{
    UART0_WriteString("\033[2J\033[H"); // Clear terminal screen
    UART0_WriteString("================================\r\n");
    UART0_WriteString("    Multi-Function System       \r\n");
    UART0_WriteString("================================\r\n");
    UART0_WriteString("[1] Calculator\r\n");
    UART0_WriteString("[2] SysTick Timer\r\n");
    UART0_WriteString("[3] Stopwatch\r\n");
    UART0_WriteString("[4] Temperature Monitor\r\n");
    UART0_WriteString("================================\r\n");
    UART0_WriteString("Select an option (1-4): ");
}

/* --------------------------------------------------------------------------
 * Temperature Helper
 * -------------------------------------------------------------------------- */
static float ConvertTemp(int adc)
{
    return 147.5 - ((75.0 * 3.3 * adc) / 4096.0);
}

/* --------------------------------------------------------------------------
 * Stopwatch Helpers
 * -------------------------------------------------------------------------- */
void UART0_Send2Digits(uint32_t num)
{
    UART0_WriteChar((num / 10) + '0');
    UART0_WriteChar((num % 10) + '0');
}

void UART0_PrintTime(void)
{
    UART0_WriteString("\r[ Time: ");
    UART0_Send2Digits(minutes);
    UART0_WriteChar(':');
    UART0_Send2Digits(seconds);
    UART0_WriteString(" ]   ");
}

/* --------------------------------------------------------------------------
 * Calculator Float Math Helpers
 * -------------------------------------------------------------------------- */
static void reverse(char* str, int len) {
    int i = 0, j = len - 1, temp;
    while (i < j) { temp = str[i]; str[i] = str[j]; str[j] = temp; i++; j--; }
}

static int intToStr(int x, char str[], int d) {
    int i = 0;
    if (x == 0) str[i++] = '0';
    while (x) { str[i++] = (x % 10) + '0'; x = x / 10; }
    while (i < d) str[i++] = '0';
    reverse(str, i);
    str[i] = '\0';
    return i;
}

static void float_to_string(float n, char* res, int afterpoint) {
    bool neg = false;
    if (n < 0.0f) { neg = true; n = -n; }
    int ipart = (int)n;
    float fpart = n - (float)ipart;
    int i = intToStr(ipart, res, 0);

    if (neg) {
        for (int j = i; j >= 0; j--) res[j + 1] = res[j];
        res[0] = '-'; i++;
    }
    if (afterpoint != 0) {
        res[i] = '.';
        float mult = 1.0f;
        for (int j = 0; j < afterpoint; j++) mult *= 10.0f;
        fpart = fpart * mult;
        fpart += 0.5f; 
        intToStr((int)fpart, res + i + 1, afterpoint);
    }
}

static float string_to_float(const volatile char *str, int *bytes_read) {
    float res = 0.0f, frac = 1.0f;
    bool decimal = false, neg = false;
    int i = 0;

    while (str[i] == ' ') i++; 
    if (str[i] == '-') { neg = true; i++; }

    for (; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') {
            if (decimal) { frac /= 10.0f; res += (str[i] - '0') * frac; } 
            else res = res * 10.0f + (str[i] - '0');
        } 
        else if (str[i] == '.') decimal = true;
        else break; 
    }
    if (bytes_read) *bytes_read = i;
    return neg ? -res : res;
}

/* --------------------------------------------------------------------------
 * Hardware Timing & LED Functions (For Alarms)
 * -------------------------------------------------------------------------- */
static void LEDs_Init(void) {
    volatile uint32_t delay;
    SYSCTL_RCGCGPIO_R |= 0x20;     
    delay = SYSCTL_RCGCGPIO_R;     
    (void)delay;

    GPIO_PORTF_DIR_R |= 0x06;      
    GPIO_PORTF_DEN_R |= 0x06;      
    GPIO_PORTF_DATA_R &= ~0x06;    
}

void SysTick_Init(void) {
    NVIC_ST_CTRL_R = 0;
    NVIC_ST_RELOAD_R = 16000000 - 1; // 1 sec
    NVIC_ST_CURRENT_R = 0;
    NVIC_ST_CTRL_R = 0x07;
}

static void DelayMs(uint32_t ms) {
    uint32_t count = ms * 5333; 
    while(count--) {
        __asm("nop");
    }
}

static void Blink_Error_LED(void) {
    for(int i = 0; i < 3; i++) {
        GPIO_PORTF_DATA_R |= 0x02;  
        DelayMs(500);               
        GPIO_PORTF_DATA_R &= ~0x02; 
        DelayMs(500);               
    }
}

static void Blink_Timer_LED(void) {
    for(int i = 0; i < 3; i++) {
        GPIO_PORTF_DATA_R |= 0x04;  
        DelayMs(500);               
        GPIO_PORTF_DATA_R &= ~0x04; 
        DelayMs(500);               
    }
}
