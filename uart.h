#ifndef UART_H_
#define UART_H_

void UART0_Init(void);
void UART0_WriteChar(char c);
void UART0_WriteString(const char *str);

#endif /* UART_H_ */