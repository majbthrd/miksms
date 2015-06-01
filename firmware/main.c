/*
    a minimal 3GPP interface to emulate the sending of SMS messages over GSM

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
#include <string.h>
#include "usb_config.h"
#include "usb_ch9.h"
#include "usb_cdc.h"

static const char ok_response   [] = "OK\r\n";
static const char cmgs_request  [] = "+CMGS=";
static const char cpin_request  [] = "+CPIN?";
static const char cpin_response [] = "+CPIN: READY\r\n";
static const char reset_request [] = "Z9";

static void send_string(const char *msg);

extern void user_handle_message(void);
extern void user_init(void);
extern void user_service(void);

uint8_t scratchpad[192];
uint8_t scratchpad_index;

static uint8_t *in_buf;
static uint8_t toPC_count;

static const uint8_t *out_buf;
static uint8_t fromPC_count;
static uint8_t keystroke, read_index, nibble;

static uint8_t incoming[EP_2_OUT_LEN];

static enum
{
	IDLE,
	CMD,
	LF,
	FIRST,
	SECOND,
	MESSAGE,
} state;

int main(void)
{
	static bit have_nibble;

	usb_init();

	user_init();

	in_buf = usb_get_in_buffer(2);

	/* there is nothing yet to send to the PC */
	toPC_count = 0;
	/* nothing yet has been received from the PC */
	fromPC_count = 0; read_index = 0;

	state = IDLE;

	for (;;)
	{
start_of_loop:
		usb_service();

		user_service();

		/* if USB isn't configured, there is no point in proceeding further */
		if (!usb_is_configured())
			continue;

		/* proceed further only if the PC can accept more data */
		if (usb_in_endpoint_halted(2) || usb_in_endpoint_busy(2))
			continue;

		/*
		if we've reached here, the USB stack can accept more; 
		if we have data to send to the PC, we hand it over
		*/
		if (toPC_count)
		{
			usb_send_in_buffer(2, toPC_count);
			toPC_count = 0;
		}

		/*
		so as to ensure that we do not generate more response data than we can store in in_buf[]
		we bail the while-loop whenever there is a response to transmit back to the PC
		*/

		while (read_index < fromPC_count)
		{
			keystroke = incoming[read_index++];

			if (0x0D == keystroke)
			{
				/* echo CRLF */
				send_string(ok_response + 2);
			}
			else if (0x1B == keystroke)
			{
				/* ESC resets the state machine */
				state = IDLE;
			}

			switch (state)
			{
			case IDLE:
				if (0x0D == keystroke)
				{
					state = LF;
				}
				break;
			case LF:
				state = (0x0A == keystroke) ? FIRST : IDLE;
				break;
			case CMD:
				if (0x0D == keystroke)
				{
					if (0 == strncmp(cmgs_request, scratchpad, sizeof(cmgs_request) - 1))
					{
						send_string("> ");
						state = MESSAGE;
						scratchpad_index = 0;
						have_nibble = false;
						break;
					}
					else if (0 == strncmp(cpin_request, scratchpad, sizeof(cpin_request) - 1))
					{
						send_string(cpin_response);
					}
					else if (0 == strncmp(reset_request, scratchpad, sizeof(reset_request) - 1))
					{
						/* enable watchdog; the code doesn't clear the watchdog, so it will eventually reset */
						WDTCONbits.SWDTEN = 1;
					}

					send_string(ok_response);
					state = LF;
				}
				else if (scratchpad_index < (sizeof(scratchpad) - 1))
				{
					scratchpad[scratchpad_index++] = keystroke;
					scratchpad[scratchpad_index] = '\0';
				}
				break;
			case FIRST:
				if ('A' == keystroke)
					state = SECOND;
				else
					state = IDLE;
				break;
			case SECOND:
				if ('T' == keystroke)
				{
					state = CMD;
					scratchpad_index = 0;
					scratchpad[0] = '\0';
				}
				else
				{
					state = IDLE;
				}
				break;
			case MESSAGE:
				if (0x1A == keystroke)
				{
					user_handle_message();
					send_string(ok_response);
					state = IDLE;
				}
				else
				{
					nibble <<= 4;

					if ( (keystroke >= 'A') && (keystroke <= 'F') )
						nibble |= 10 + (keystroke - 'A');
					else if ( (keystroke >= '0') && (keystroke <= '9') )
						nibble |= (keystroke - '0');

					if ( have_nibble && (scratchpad_index < sizeof(scratchpad)) )
						scratchpad[scratchpad_index++] = nibble;

					have_nibble = !have_nibble;
				}
				break;
			}

			/* if we need to send a response, go right back to the start of the main loop */
			if (toPC_count)
				goto start_of_loop;
		}

		/* if we pass this test, we are committed to make the usb_arm_out_endpoint() call */
		if (!usb_out_endpoint_has_data(2))
			continue;

		/* ask USB stack for more data from the PC */
		fromPC_count = usb_get_out_buffer(2, &out_buf);

		/* make our copy of the data for more leisurely parsing */
		memcpy(incoming, out_buf, fromPC_count);
		/* rewind so as to start reading from the beginning */
		read_index = 0;

		/*
		indicate to USB stack that we can receive more data
		assume that as soon as we call this, out_buf will start filling again
		*/
		usb_arm_out_endpoint(2);
	}
}

static void send_string(const char *msg)
{
	while (*msg)
		in_buf[toPC_count++] = *msg++;
}
