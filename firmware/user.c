/*
    a minimal 3GPP interface to emulate the sending of SMS messages over GSM
    this variant is the trick fade WS281X LED control code

    Copyright (C) 2015 Peter Lawrence

    this was intended to connect to a Mikrotik RouterOS device with USB port
    the user can extend the message parsing code to intepret it as desired

    based on top of M-Stack USB driver stack by Alan Ott, Signal 11 Software

    Permission is hereby granted, free of charge, to any person obtaining a 
    copy of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation 
    the rights to use, copy, modify, merge, publish, distribute, sublicense, 
    and/or sell copies of the Software, and to permit persons to whom the 
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in 
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.
*/

#include "usb.h"
#include <xc.h>

#define LED_COUNT       24
#define FADE_DELAY_LOG2 4

struct ws_led_struct
{
	uint8_t g, r, b; /* the order is critical: the WS281x expects green, red, then blue */
};

struct bookkeep_struct
{
	uint16_t increment;
	uint8_t fraction;
};

struct target_struct
{
	struct ws_led_struct leds;
	uint8_t fade_delay;
	struct bookkeep_struct bookkeep_g, bookkeep_r, bookkeep_b;
};

extern uint8_t scratchpad[];
extern uint8_t scratchpad_index;

static void set_target(uint8_t ledn);
static void adjust_led(volatile uint8_t *current, struct bookkeep_struct *bookkeep);
static void shift_leds(void);

/*
local variables
*/

/* array storing the current state of all LEDs */
static struct ws_led_struct leds[LED_COUNT + 1];

/* pointer used by ISR to incrementally read leds[] array */
static uint8_t *ptr;

/* array storing the target LED values (and calculated step values to get there) */
static struct target_struct targets[LED_COUNT];

void user_init(void)
{
	/* SPI (WS281x) init */
	SSP1STAT = 0x40;
	SSP1CON1 = 0x20;
	ANSELCbits.ANSC2 = 0;
	TRISCbits.TRISC2 = 0;

	/* enable everything but global interrupts in preparation for SPI interrupt */
	PIR1bits.SSP1IF = 0;
	PIE1bits.SSP1IE = 1;
	INTCONbits.PEIE = 1;

	/* configure TMR2 for 50Hz (50.08Hz) */
	T2CONbits.T2CKPS = 0b11;    /* Prescaler is 64 */
	T2CONbits.T2OUTPS = 0b1111; /* Postscaler is 16 */
	PR2 = 234;
	T2CONbits.TMR2ON = 1;       /* enable TMR2 */
}

/*
canned lookup table values for rainbow
*/

static const struct ws_led_struct rainbow[26] =
{
	{ 0, 253, 3 },
	{ 0, 226, 30 },
	{ 0, 200, 56 },
	{ 0, 173, 83 },
	{ 0, 146, 110 },
	{ 0, 116, 140 },
	{ 0, 90, 166 },
	{ 0, 63, 192 },
	{ 0, 37, 218 },
	{ 0, 10, 245 },
	{ 39, 0, 217 },
	{ 93, 0, 163 },
	{ 145, 0, 111 },
	{ 184, 0, 71 },
	{ 210, 0, 45 },
	{ 240, 0, 15 },
	{ 245, 23, 0 },
	{ 218, 77, 0 },
	{ 192, 129, 0 },
	{ 164, 171, 0 },
	{ 135, 171, 0 },
	{ 108, 171, 0 },
	{ 82, 173, 0 },
	{ 55, 200, 0 },
	{ 29, 226, 0 },
	{ 0, 255, 0 },
};

/*
canned lookup table values for grayscale
*/

static const struct ws_led_struct grayscale[10] =
{
	{ 0, 0, 0 },
	{ 28, 28, 28 },
	{ 56, 56, 56 },
	{ 85, 85, 85 },
	{ 113, 113, 113 },
	{ 141, 141, 141 },
	{ 170, 170, 170 },
	{ 198, 198, 198 },
	{ 226, 226, 226 },
	{ 255, 255, 255 },
};

/*
message decoder called by main code
*/

void user_handle_message(void)
{
	uint8_t data, result, bits, len, index, val, ledn;

	/* position 3 has the length of the telephone number */
	index = scratchpad[3];
	/* convert that to bytes */
	index = (index + 1) >> 1;

	/* advance to the beginning of the message (noting its length) */
	index += 7;
	len = scratchpad[index++];

	bits = 7; data = 0; ledn = 1;

	/* recover the 7-bit septet characters one by one */

	while ( len-- && (index <= scratchpad_index) )
	{
		result = data & ((1 << (7 - bits)) - 1);
		if (bits)
		{
			data = scratchpad[index++];
			result |= data << (7 - bits);
			data >>= bits;
		}

		if (0 == bits)
			bits = 7;
		else
			bits--;

		/* recover the character */
		val = result & 0x7F;

		/*
		decode the character
		*/

		if ('>' == val)
		{
			shift_leds();
			ledn = 1;
			continue;
		}
		else if (' ' != val)
		{
			if ((val >= 'a') && (val <= 'z'))
			{
				/* rainbow color requested */
				leds[0] = rainbow[val - 'a'];
				set_target(ledn);
			}
			else if ((val >= '0') && (val <= '9'))
			{
				/* grayscale color requested */
				leds[0] = grayscale[val - '0'];
				set_target(ledn);
			}
			else if ('X' == val)
			{
				/* turn all LEDs off */
				leds[0] = grayscale[0];
				set_target(0);
			}
		}

		ledn++;
	}
}

void user_service(void)
{
	uint8_t count;
	struct target_struct *tpnt;
	struct ws_led_struct *lpnt;

	/* check if the timer has fired... */
	if (TMR2IF)
	{
		/* ... and if so, acknowledge it */
		TMR2IF = 0;

		/*
		this is the secret sauce to efficiently write to the WS281x
		rather than some pointlessly complicated fine-tuned delay loops, etc.,
		we just fire and forget using the interrupt service routine
		*/
		ptr = (uint8_t *)&leds[1];
		INTCONbits.GIE = 1;
		PIR1bits.SSP1IF = 1;

		/*
		whilst the ISR takes care of talking to the WS281x, 
		we can focus on the heavy task of fading the LEDs
		*/
		tpnt = &targets[0]; lpnt = &leds[1];
		for (count = 0; count < LED_COUNT; count++)
		{
			if (0 == tpnt->fade_delay)
			{
				/* the fade has elapsed for this LED; write the final values */
				*lpnt = tpnt->leds;
			}
			else
			{
				/* the fade isn't finished, but we are one step (20ms) closer */
				tpnt->fade_delay--;

				/* do that embedded voodoo that you do to make the fade happen */
				adjust_led(&lpnt->g, &tpnt->bookkeep_g);
				adjust_led(&lpnt->r, &tpnt->bookkeep_r);
				adjust_led(&lpnt->b, &tpnt->bookkeep_b);
			}

			tpnt++; lpnt++;
		}
	}
}

void interrupt isr()
{
	static uint8_t bit_position;
	static uint8_t byte_count;
	static uint8_t current_byte;

	/* check if SSP1IF interrupt has fired... */
	if (PIR1bits.SSP1IF)
	{
		/* ... and acknowledge it by clearing SSP1IF flag */
		PIR1bits.SSP1IF = 0;

		if (0 == bit_position)
		{
			/* 
			if bit_position is zero, we've exhausted all the bits in the previous byte 
			and need to load current_byte with the next byte
			*/
			if ((LED_COUNT * sizeof(struct ws_led_struct)) == byte_count)
			{
				/*
				we've reached the end of the LED data, and 
				the interrupt routine's work is done;
				so, we disable the interrupt and bail
				*/
				INTCONbits.GIE = 0;
				byte_count = 0;
				return;
			}

			/* load next byte into current_byte */			
			current_byte = *ptr++;
			byte_count++;
		}

		/* WS281X expects long pulse for '1' and short pulse for '0' */
		SSP1BUF = (current_byte & 0x80) ? 0xFF : 0xF0;

		/* preemptively shift next bit into position and update bit_position */
		current_byte <<= 1;
		bit_position = (bit_position + 1) & 0x7;		
	}
}

static void calc_increment(volatile uint8_t *current, volatile uint8_t *target, struct bookkeep_struct *bookkeep)
{
	uint8_t updir, lsb;
	uint16_t distance;

	/* are we going UP or DOWN? */
	updir = *target > *current;

	/* figure the distance that we have to travel */
	if (updir)
		distance = (uint16_t)*target - (uint16_t)*current;
	else
		distance = (uint16_t)*current - (uint16_t)*target;

	/*
	power of 2 multiple by 256
	the 0 to 255 distance value is now super-sized to 0 to 65280
	*/
	distance <<= 8;

	bookkeep->increment = 0;
	bookkeep->fraction = 0;

	/*
	more efficient implementation of:
	bookkeep->increment = distance / (1 << FADE_DELAY_LOG2);
	*/
	bookkeep->increment = distance >> FADE_DELAY_LOG2;

	/*
	by mangling the computed result slightly, we can store our computed flag for later use
	*/
	lsb = bookkeep->increment & 1;
	if ( ((0 != updir) && (0 == lsb)) || ((0 == updir) && (0 != lsb)) )
		bookkeep->increment--;
}

static void set_target(uint8_t ledn)
{
	uint8_t index;
	struct target_struct *tpnt = &targets[0];
	struct ws_led_struct *lpnt = &leds[1];

	/* sequence through all the LEDs in turn */
	for (index = 1; index <= LED_COUNT; index++)
	{
		/* check if this LED is being written to */
		if (!ledn || (ledn == index))
		{
			/* write the new target value */
			tpnt->leds = leds[0];
			tpnt->fade_delay = (1 << FADE_DELAY_LOG2);

			/* calculate the fade values to aim for each of the target colors */
			calc_increment(&lpnt->g, &leds[0].g, &tpnt->bookkeep_g);
			calc_increment(&lpnt->r, &leds[0].r, &tpnt->bookkeep_r);
			calc_increment(&lpnt->b, &leds[0].b, &tpnt->bookkeep_b);
		}

		/* advance to the next target and led */
		*tpnt++; *lpnt++;
	}
}

static void adjust_led(volatile uint8_t *current, struct bookkeep_struct *bookkeep)
{
	uint8_t updir, msb;
	uint16_t change;

	/* retrieve the flag we hid earlier */
	updir = bookkeep->increment & 1;

	/*
	"fraction" is an 8-bit expression of the fraction after the decimal point of the current 8-bit LED value
	the calculated "change" value is the step next LED value... expressed as a combination of whole and fraction
	we add the whole to the current LED value and write the new fraction back to "fraction"
	*/

	change = (uint16_t)bookkeep->fraction + bookkeep->increment;

	msb = change >> 8;
	if (updir)
		(*current)+=msb;
	else
		(*current)-=msb;

	bookkeep->fraction = change & 0xFF;
}

static void shift_leds(void)
{
	uint8_t ledn;
	struct target_struct *tpnt = &targets[LED_COUNT - 2];

	/* we have LED_COUNT LEDs; so, iterate the loop one less than that */
	ledn = LED_COUNT - 1;
	while (ledn)
	{
		/* set the target of LED(ledn + 1) to the target of LED(ledn) */
		leds[0] = tpnt->leds;
		set_target(ledn + 1);

		/* backtrack another step to the next previous LED */
		tpnt--; ledn--;
	}
}
