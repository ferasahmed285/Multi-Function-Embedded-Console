/******************************************************************************
 * File: GPTM.h
 * Module: General-Purpose Timer Module
 * Description: Header file for TM4C123GH6PM Timer Driver
 ******************************************************************************/

#ifndef GPTM_H_
#define GPTM_H_

#include <stdint.h>

/*
 * Timer1A_Init
 * Initializes Timer 1A in 32-bit periodic mode.
 * Configured to generate an interrupt every 1 second based on a 16 MHz system clock.
 */
void Timer1A_Init(void);

#endif /* GPTM_H_ */