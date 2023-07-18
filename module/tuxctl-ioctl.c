/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

static int ackFlag = 1;
static unsigned long ledFlag = 0;
static unsigned char pressed_button;
spinlock_t lock;

int tux_init(struct tty_struct* tty);
int tux_buttons(struct tty_struct* tty, unsigned long arg);
int tux_set_led(struct tty_struct* tty, unsigned long arg);
static void handler_reset(struct tty_struct* tty);

// 7-segment hex to actual display mapping 
static unsigned char hex2disp[16] = { 0xE7, 0x06, 0xCB, 0x8F, 0x2E, 0xAD,
									  0xED, 0x86, 0xEF, 0xAF, 0xEE, 0x6D,
									  0xE1, 0x4F, 0xE9, 0xE8 }; 
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

	switch(a){
		case MTCP_ACK:
		if(ackFlag != 0){
			tux_set_led(tty, ledFlag);
			ackFlag =0;
		}	
		break;
		case MTCP_BIOC_EVENT:
			// bitmasking 
			spin_lock(&lock);
			b = b & 0x0F;
			c = c << 4; 
			c = c & 0XF0;

			pressed_button = b + c; // saved the button pressed
			spin_unlock(&lock);
			break;
		case MTCP_RESET:
			handler_reset(tty);
			break;
		default:
			break;
	}
}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
/*
 * tuxctl_ioctl
 *   DESCRIPTION: IO control for the tux controller
 *   INPUTS: tty_struct* tty, file* file (ignored), usigned cmd (ignored), unsigned long arg
 *   OUTPUTS: none
 *   RETURN VALUE: return 0 for success, -EINVAL for fail
 *   SIDE EFFECTS: changes the state of the controller
 */ 

int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
	case TUX_INIT:
		return tux_init(tty);
	case TUX_BUTTONS:
		return tux_buttons(tty, arg);
	case TUX_SET_LED:
		return tux_set_led(tty, arg);
	case TUX_LED_ACK:
		return 0;
	case TUX_LED_REQUEST:
		return 0;
	case TUX_READ_LED:
		return 0;
	default:
	    return -EINVAL;
    }
}

/*
 * handler_reset
 *   DESCRIPTION: function that executes when the handler calls reset
 *   INPUTS: tty_struct* tty
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: changes LED on controller
 */  
static void handler_reset(struct tty_struct* tty){
	unsigned char arr[2];
	arr[0] = MTCP_LED_USR;
	arr[1] = MTCP_BIOC_ON;
	lock = SPIN_LOCK_UNLOCKED;
	tuxctl_ldisc_put(tty, arr, 2);
	tux_set_led(tty, ledFlag); // clear LED display
}

/*
 * tux_init
 *   DESCRIPTION: initiates the TUX controller variables 
 *   INPUTS: tty_struct* tty
 *   OUTPUTS: none
 *   RETURN VALUE: always return 0
 *   SIDE EFFECTS: changes LED on controller
 */  

int tux_init(struct tty_struct* tty){
	unsigned char arr[2];

	arr[0] = MTCP_LED_USR;
	arr[1] = MTCP_BIOC_ON;
	lock = SPIN_LOCK_UNLOCKED;
	tuxctl_ldisc_put(tty, arr, 2);
	pressed_button = 0x00;
	ackFlag = 1;
	ledFlag = 0x00000000;
	tux_set_led(tty, ledFlag);
	return 0;
}

/*
 * tux_buttons
 *   DESCRIPTION: tells the user what button on the controller was pressed
 *   INPUTS: tty_struct* tty, unsigned long arg
 *   OUTPUTS: none
 *   RETURN VALUE: 0 for success, -EINVAL for fail
 *   SIDE EFFECTS: none
 */  
int tux_buttons(struct tty_struct* tty, unsigned long arg){
	unsigned long* arg_ptr = (unsigned long*)arg;
	int ret;
	spin_lock(&lock);
	ret = copy_to_user(arg_ptr, &pressed_button, 1);
	spin_unlock(&lock);

	if(ret > 0)
		return -EINVAL;
	return 0;
}

/*
 * tux_set_led
 *   DESCRIPTION: function that executes when the handler calls reset
 *   INPUTS: tty_struct* tty, unsigned long arg
 *   OUTPUTS: none
 *   RETURN VALUE: 0 for success, -EINVAL for fail
 *   SIDE EFFECTS: changes LED on controller
 */  
int tux_set_led(struct tty_struct* tty, unsigned long arg){
	unsigned char LEDisplay; 
	unsigned char buf[6] = {[0 ... 5] = 0x00}; // initialize filled with 0
	unsigned long bitmask = 0x01; // mask for LED selector
	int i;
	unsigned int LED_on, LED_decimal, data;
	unsigned long arg_copy = arg; // used for getting specifc parts of the arg without changing arg
	if(ackFlag == 1)
		return -EINVAL;
	ackFlag = 1;

	data = arg & 0x0000FFFF; // get the hex data on to data 

	buf[0] = MTCP_LED_SET;
	arg_copy = arg_copy & 0x000F0000; //getting which LEDs should be turned on  
	arg_copy = arg_copy >> 16;
	buf[1] = 0x0F; 			// now holds which LEDs should be turned on 
	LED_on = arg_copy;
	ledFlag = arg;
	arg_copy = arg;
	arg_copy = arg_copy & 0x0F000000; // getting lower 4 bits of highest byte (decimal points)
	LED_decimal = arg_copy >> 24;

	for(i=0; i<4; i++){ // loop through the 4 LEDs
		LEDisplay = 0;
		if(LED_on & bitmask){
			LEDisplay = hex2disp[data & 0x0F]; // get the hex to display mapping
		}
		if(LED_decimal & bitmask)
			LEDisplay = LEDisplay + 0x10;	// setting the decimal points
		
		buf[2+i] = LEDisplay;

		data = data >> 4; // get the next 4 hex number to lowest 4 bits
		bitmask = bitmask << 1; // shift the bitmask to get the next LED
	}
	tuxctl_ldisc_put(tty, buf, 6);
	return 0;
}
