/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include <stdio.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

#define tp

int background()
{
	int j;
	int x = 0;
	int grainsize = 4;
	int g_taskProcessed = 0;

	IOWR(LED_PIO_BASE, 0, 1); // turn LEDs BIT0 to ON to denote START of background task
	for(j = 0; j < grainsize; j++)
	{
		g_taskProcessed++;
	}
	IOWR(LED_PIO_BASE, 0, 0); // turn LEDs BIT0 to OFF to denote END of background task
	return x;
}

static void labISR (void* context, alt_u32 id)
{
	IOWR(LED_PIO_BASE, 0, 2); // turn LEDs BIT1 to ON to denote START of ISR
	IOWR(RESPONSE_OUT_BASE, 0, 1);	// send response
	IOWR(RESPONSE_OUT_BASE, 0, 0);

	IOWR(STIMULUS_IN_BASE, 3, 0);	// clear interrupt flag
	IOWR(LED_PIO_BASE, 0, 0); // turn LEDs BIT1 to OFF to denote END of ISR
}

int main()
{
#ifdef intr
	printf("Interrupt Mode\n");
	printf("period, pulse width, bg tasks run, avg latency, missed pulses\n");

	int pb0;

	while (pb0 != 14)	// wait for pb0 button press
	{
		pb0 = IORD(BUTTON_PIO_BASE,0);
	}

	int n;	// n = pulse width
	for(n = 1; n <= 2500; n++)
	{
		IOWR(LED_PIO_BASE, 0, 4); // flash LEDs BIT2 to denote START of test
		IOWR(LED_PIO_BASE, 0, 0);

		int busy = 1;	// check if EGM is busy, initialize to true
		int bg = 0;		// BG task counter

		IOWR(EGM_BASE, 0, 0);	// disable EGM before enabling
		IOWR(EGM_BASE, 2, 2*n);	// set period = 2*pulse width
		IOWR(EGM_BASE, 3, n);	// set pulse width
		alt_irq_register( STIMULUS_IN_IRQ, (void *)0, labISR );	// register ISR
		IOWR(STIMULUS_IN_BASE, 2, 1);	// enable (mask) ISR
		IOWR(EGM_BASE, 0, 1);	//enable EGM

		while (busy)
		{
			background();	// run BG task and increment counter as long as EGM is busy
			bg++;

			busy = IORD(EGM_BASE, 1);	//check if EGM is busy
		}

		int latency = IORD(EGM_BASE, 4);	// read average latency and missed pulses
		int missed = IORD(EGM_BASE, 5);

		printf("%d, %d, %d, %d, %d \n", 2*n, n, bg, latency, missed);	// print results for each test
	}

	IOWR(EGM_BASE, 0, 0); // disable EGM

	return 0;
#endif

#ifdef tp
	printf("Tight Polling Mode\n");
	printf("period, pulse width, bg tasks run, avg latency, missed pulses\n");

	int pb0 = 0;

	while (pb0 != 14)	// wait for pb0 button press
	{
		pb0 = IORD(BUTTON_PIO_BASE,0);
	}

	int n;	// n = pulse width
	for(n = 1; n <= 2500; n++)
	{
		IOWR(LED_PIO_BASE, 0, 4); // flash LEDs BIT2 to denote START of test
		IOWR(LED_PIO_BASE, 0, 0);

		int busy = 1;	// check if EGM is busy, initialize to true
		int bg = 0;		// BG task counter
		int stimulus;	// value of stimulus
		int new_pulse = 1;	// new edge detection variable, first edge is always new
		int first_pulse = 1;	// disable for every pulse past the first pulse
		int char_bg = 0;	// characterized number of background tasks
		int run_bg;		// determine whether or not to run bg tasks

		IOWR(EGM_BASE, 0, 0);	// disable EGM before enabling
		IOWR(EGM_BASE, 2, 2*n);	// set period = 2*pulse width
		IOWR(EGM_BASE, 3, n);	// set pulse width
		IOWR(EGM_BASE, 0, 1);	// enable EGM

		while (busy)
		{
			stimulus = IORD(STIMULUS_IN_BASE, 0);

			if (stimulus == 1 && new_pulse == 1)	// if a new pulse edge is detected
			{
				IOWR(RESPONSE_OUT_BASE, 0, 1);	// send response
				IOWR(RESPONSE_OUT_BASE, 0, 0);
				new_pulse = 0;
				if (first_pulse == 0)
				{
					int i;
					for (i = char_bg - 1; i > 0; i--)	// only run after first pulse is completed
					{
						background();	// run BG task and increment counter as long as EGM is busy
						bg++;
					}
				}
			}


			if (stimulus == 0)	// new edge incoming if stimulus is 0
			{
				new_pulse = 1;
			}

			while (first_pulse == 1)	// characterization
			{
				background();	// run BG task and increment counter as long as EGM is busy
				bg++;
				char_bg++;	// increment characterization number of bg tasks
				stimulus = IORD(STIMULUS_IN_BASE, 0);
				if (stimulus == 0)	// new edge incoming if stimulus is 0
				{
					new_pulse = 1;
				}
				if (stimulus == 1 && new_pulse == 1)
				{
					first_pulse = 0;
				}
			}

			busy = IORD(EGM_BASE, 1);	//check if EGM is busy
		}

		int latency = IORD(EGM_BASE, 4);	// read average latency and missed pulses
		int missed = IORD(EGM_BASE, 5);

		printf("%d, %d, %d, %d, %d \n", 2*n, n, bg, latency, missed);	// print results for each test
	}

	IOWR(EGM_BASE, 0, 0); // disable EGM

	return 0;
#endif
}
