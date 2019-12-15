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
//cleared LED variable
#define LED_CLEAR 0xF0FF0000
#define BMASK 0x0F

//One lock for everything approach
static spinlock_t lock;
//reset flag
static int reset_ready = 0;
//holds value for LED
static unsigned int LED;
//holds value for button
static unsigned char btn;


//hex values for 7-seg display
char tuxctl_display[16] = {0xE7,  0x06, 0xCB, 0x8F, 0x2E, 0xAD, 0xED, 0x86, 0xEF,
	0xAF, 0xEE, 0x6D, 0xE1, 0x4F, 0xE9, 0xE8};


//initializes tux controller
int tuxctl_ioctl_tux_init(struct tty_struct* tty);
//handles tux controller reset
int tuxctl_ioctl_tux_reset(struct tty_struct* tty);
//handles button presses from tux controller
int tuxctl_ioctl_button_set(struct tty_struct* tty, unsigned long arg);
//sets values to ensure LED displays correctly
int tuxctl_ioctl_led_set(struct tty_struct* tty, unsigned long arg);

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
		//used in interrupt handler so lock not semaphore
		spin_lock(&lock);
		//a gives us which event needs to be handled
		switch(a) {
			//reset tux controller
			case MTCP_RESET:
				spin_unlock(&lock);
				tuxctl_ioctl_tux_reset(tty);
				break;
			//handle button pressed event
			case MTCP_BIOC_EVENT:
				//calculation to determine which button pressed based on incoming packet
				//masks high 4 bits of b and low 4 bits of c, then combines both packets into 1 8-bit c:b
				btn = (b & BMASK) | ((c & BMASK) << 4);
				spin_unlock(&lock);
				break;
			case MTCP_ACK:
				//handles reset completed (if reset occurred)
				if(reset_ready == 1){
					reset_ready = 0;
					spin_unlock(&lock);
					//sets LED to old value of LED
					tuxctl_ioctl_led_set(tty, LED);
					break;
				}
				else {
					spin_unlock(&lock);
					break;
				}
			default:
				spin_unlock(&lock);
				break;
		}

    /*printk("packet : %x %x %x\n", a, b, c); */
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
int
tuxctl_ioctl (struct tty_struct* tty, struct file* file,
	      unsigned cmd, unsigned long arg)
{
    switch (cmd) {
			case TUX_INIT:
				return tuxctl_ioctl_tux_init(tty);
			case TUX_BUTTONS:
				//sanity check
				if(arg == 0)
					return -EINVAL;
				else
					return tuxctl_ioctl_button_set(tty, arg);
			case TUX_SET_LED:
				return tuxctl_ioctl_led_set(tty, arg);
			default:
	    	return -EINVAL;
    }
}

/*
 * tuxctl_ioctl_tux_init(struct tty_struct* tty)
 * Description: initializes tux controller into correct settings
 * Inputs: tty - pointer to a tty_struct
 * Outputs: None
 * Returns: 0
 * Side Effects: Opens the port to write into the TUX, sets TUX into LED User mode, turns BIOC on
 */
int tuxctl_ioctl_tux_init(struct tty_struct* tty) {
	char init[2] = {MTCP_LED_USR, MTCP_BIOC_ON};
	spin_lock_init(&lock);
	//set TUX into LED User mode and read button presses
	tuxctl_ldisk_put(tty, init, 2);
	//initializes values of button and LED
	btn = 0xFF;
	LED = LED_CLEAR;
	//clears LED display by passing MTCP_LED_SET value and then value for LED cleared
	tuxctl_ldisk_put(tty, MTCP_LED_SET, 1);
	tuxctl_ldisk_put(tty, 0x0F, 1);
	int i;
	for(i = 0; i < 4; i++)
		tuxctl_ldisk_put(tty, 0x00, 1);
	return 0;
}


/*
 * tuxctl_ioctl_tux_reset(struct tty_struct* tty)
 * Description: resets TUX controller to allow button presses and restores LED values
 * Inputs: tty - pointer to a tty_struct
 * Outputs: None
 * Returns: 0
 * Side Effects: Resets button usage and LED display on TUX controller
 */
int tuxctl_ioctl_tux_reset(struct tty_struct* tty) {
	spin_lock(&lock);
	char reset[2] = {MTCP_LED_USR, MTCP_BIOC_ON};
	//resets TUX into LED user mode and reads button presses
	tuxctl_ldisk_put(tty, reset, 2);
	//ready to reset again
	reset_ready = 1;
	spin_unlock(&lock);
	return 0;
}

/*
 * tuxctl_ioctl_button_set(struct tty_struct* tty, unsigned long arg)
 * Description: copies value of button pressed to user
 * Inputs: tty - pointer to a tty_struct, arg - value of button pressed
 * Outputs: None
 * Returns: 0 if successful -EINVAL if unsuccessful
 * Side Effects: writes button pressed into userspace
 */
int tuxctl_ioctl_button_set(struct tty_struct* tty, unsigned long arg) {
	spin_lock(&lock);
	//call copy_to_user funciton with pointer to arg and value of button
	//tests to see if successful and returns value accordingly
	if(copy_to_user((int*)arg, &btn, 1)) {
		spin_unlock(&lock);
		return -EINVAL;
	}
	else {
		spin_unlock(&lock);
		return 0;
	}
}

/*
 * tuxctl_ioctl_led_set(struct tty_struct* tty, unsigned long arg)
 * Description: sets seven segment display on TUX controller
 * Inputs: tty - pointer to a tty_struct, arg - value of time passed
 * Outputs: Time onto Tux controller
 * Returns: 0
 * Side Effects: Sets LEDs to time elapsed
 */
int tuxctl_ioctl_led_set(struct tty_struct* tty, unsigned long arg) {
	//number of seconds passed
	unsigned int time[3] = {(unsigned int)arg, , ((unsigned int)arg)>>16, ((unsigned int)arg)>>24};
	//holds value for seven segment display
	unsigned char sev_seg_display[4];
	int i; //loop counter
	spin_lock(&lock);
	LED = time[0]; //lock while setting saved LED value (global variable)
	spin_unlock(&lock);
	//clear value of LED
	tuxctl_ldisk_put(tty, MTCP_LED_SET, 1);
	tuxctl_ldisk_put(tty, 0x0F, 1);
	for(i = 0; i < 4; i++)
		tuxctl_ldisk_put(tty, 0x00, 1);
	//mask upper 16 bits of time
	unsigned int seconds = time[0] & 0x0000FFFF;
	//bitmasks
	unsigned int bitmask = 0x000F;
	unsigned int bit = 0x1
	for(i = 0; i < 4; i++) {
		//look at each byte from elapsed time and set seven segment display to correct values accoridngly
		sev_seg_display[i] = tuxctl_display[(seconds & (bitmask << (4*i))) >> (4 * i)];
		//shift the value over to make the high-bit the most significant bit
		sev_seg_display[i] = sev_seg_display[i] | (((time[2] & BMASK) & (bit << i) >> i) << 4);
	}
	unsigned char LEDs[4];
	/*
	* enable MTCP_LED_SET and set the values to the LEDs to those that
	* were calculated for the seven segment display while checking if
	* the values satisfy the position as required with a sanity check
	*/
	LEDs[0] = MTCP_LED_SET;
	LEDs[1] = time[1];
	int led_val = 2;
	for(i = 0; i < 4; i++) {
		//tests to see if the LSB in time[1] (num_seconds shifted over by 16)
		//then masked and checked for each bit does in fact equal 1
		if(((time[1] & bitmask) & (bit << i)) >> i) {
			LEDs[led_val] = sev_seg_display[i];
			led_val++;
		}
	}
	//pushes these values to set display on TUX controller
	tuxctl_ldisk_put(tty, LEDs, led_val);
	return 0
}
