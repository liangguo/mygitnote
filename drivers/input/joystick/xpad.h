/*
 * Xbox gamepad driver with Xbox 360 wired/wireless support
 *
 * Last Modified:	2 March 2009
 *			Mike Murphy <mamurph@cs.clemson.edu>
 *
 * Copyright (c) 2002 Marko Friedemann <mfr@bmx-chemnitz.de>
 *               2004 Oliver Schwartz <Oliver.Schwartz@gmx.de>,
 *                    Steven Toth <steve@toth.demon.co.uk>,
 *                    Franz Lehner <franz@caos.at>,
 *                    Ivan Hawkes <blackhawk@ivanhawkes.com>
 *               2005 Dominic Cerquetti <binary1230@yahoo.com>
 *               2006 Adam Buchbinder <adam.buchbinder@gmail.com>
 *               2007 Jan Kratochvil <honza@jikos.cz>
 *               2009 Clemson University
 *		      (contact: Mike Murphy <mamurph@cs.clemson.edu>)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 * This driver is based on:
 *  - information from     http://euc.jp/periphs/xbox-controller.ja.html
 *  - the iForce driver    drivers/char/joystick/iforce.c
 *  - the skeleton-driver  drivers/usb/usb-skeleton.c
 *  - Xbox 360 information http://www.free60.org/wiki/Gamepad
 *  - xboxdrv docs         http://pingus.seul.org/~grumbel/xboxdrv/
 *
 * Thanks to:
 *  - ITO Takayuki for providing essential xpad information on his website
 *  - Vojtech Pavlik     - iforce driver / input subsystem
 *  - Greg Kroah-Hartman - usb-skeleton driver
 *  - XBOX Linux project - extra USB id's
 *
 * TODO:
 *  - fix "analog" buttons (reported as digital now)
 *  - need USB IDs for other dance pads
 *
 * Driver history is located at the bottom of this file.
 */

#ifndef _XPAD_H
#define _XPAD_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/module.h>
#include <linux/usb/input.h>
#include <linux/workqueue.h>

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>

#ifdef CONFIG_JOYSTICK_XPAD_LEDS
#include <linux/leds.h>

struct xpad_led {
	char name[16];
	struct led_classdev led_cdev;
	struct usb_xpad *xpad;
};
#endif


#define DRIVER_AUTHOR "Marko Friedemann <mfr@bmx-chemnitz.de>"
#define DRIVER_DESC "Xbox/360 pad driver"

#define XPAD_PKT_LEN 32


/* xbox d-pads should map to buttons, as is required for DDR pads
   but we map them to axes when possible to simplify things */
#define MAP_DPAD_TO_BUTTONS    0
#define MAP_DPAD_TO_AXES       1
#define MAP_DPAD_UNKNOWN       2

/* Type of controller *interface* (original, wired 360, wireless 360) */
#define XTYPE_XBOX        0
#define XTYPE_XBOX360     1
#define XTYPE_XBOX360W    2
#define XTYPE_UNKNOWN     3

/* Type of controller (e.g. pad, guitar, other input device) */
#define XCONTROLLER_TYPE_NONE		0
#define XCONTROLLER_TYPE_PAD		1
#define XCONTROLLER_TYPE_GUITAR		2
#define XCONTROLLER_TYPE_DANCE_PAD	3
#define XCONTROLLER_TYPE_OTHER		255


/* The Xbox 360 controllers have sensitive sticks that often do not center
 * exactly. A dead zone causes stick events below a certain threshhold to be
 * reported as zero.
 *
 * The default dead zone size is 8192, which was obtained by testing a
 * wireless 360 controller with jstest(1) and consulting gaming forums for
 * a recommended dead zone for this controller. The consensus opinion was
 * 0.25 (on a scale from 0 to 1), which corresponds to 8192 (out of 32767).
 */
#define XDEAD_ZONE_DEFAULT   8192

/* Default limit for the sticks is the maximum axis value (32767), which will
 * cause the sticks to have a radial axis as designed in the hardware. To
 * enable square axis support, set the stick limits to 23170 or lower at run
 * time via the sysfs interface. */
#define XSTICK_LIMIT_DEFAULT 32767

/* Rumble normally enabled */
#define XRUMBLE_DEFAULT		1

/* Normally, trigger axes report in the range 0 to 32767 (positive axis only) */
#define XFULL_TRIGGER_AXIS_DEFAULT	0


/* Xbox 360 has a vendor-specific class, so we cannot match it with only
 * USB_INTERFACE_INFO (also specifically refused by USB subsystem), so we
 * match against vendor id as well. Wired Xbox 360 devices have protocol 1,
 * wireless controllers have protocol 129. */
#define XPAD_XBOX360_VENDOR_PROTOCOL(vend,pr) \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | \
			USB_DEVICE_ID_MATCH_INT_INFO, \
	.idVendor = (vend), \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC, \
	.bInterfaceSubClass = 93, \
	.bInterfaceProtocol = (pr)
#define XPAD_XBOX360_VENDOR(vend) \
	{ XPAD_XBOX360_VENDOR_PROTOCOL(vend, 1) }, \
	{ XPAD_XBOX360_VENDOR_PROTOCOL(vend, 129) }



/* Some of the fields in the following structure are for later use with
 * userspace applications to recognize individual controllers. The dead zones
 * and axis limits can be changed "on the fly" and are effective immediately.
 *
 * The fields labeled "ro" and "rw" are intended to be read-only and
 * read-write, respectively, when exposed in sysfs. Most of the read-only
 * fields are to support *wireless* 360 controllers. The controller_number
 * is used to set the LED, while controller_present tracks whether the
 * controller is connected to the wireless receiver. Controller type applies
 * to all models (wired and wireless), and tracks whether the device is a pad,
 * guitar, etc. for later userspace use. See the comment above regarding
 * type and unique ID detection on wireless 360 receivers.
 */
struct usb_xpad {
	struct input_dev *dev;		/* input device interface */
	struct usb_device *udev;	/* usb device */

	struct urb *irq_in;		/* urb for interrupt in report */
	unsigned char *idata;		/* input data */
	dma_addr_t idata_dma;

#if defined(CONFIG_JOYSTICK_XPAD_FF) || defined(CONFIG_JOYSTICK_XPAD_LEDS)
	struct urb *irq_out;		/* urb for interrupt out report */
	unsigned char *odata;		/* output data */
	dma_addr_t odata_dma;
	struct mutex odata_mutex;
#endif

#ifdef CONFIG_JOYSTICK_XPAD_LEDS
	struct xpad_led *led;
#endif

	char phys[64];			/* physical device path */

	int dpad_mapping;		/* map d-pad to buttons or to axes */
	int xtype;			/* type of xbox device */

	/* Work structure for moving the call to xpad_send_led_command
	 * outside the interrupt handler for packet processing */
	struct work_struct work;

	/* id packet for wireless 360 controller */
	unsigned char *id_packet;

	int controller_number;		/* controller # (1-4) for 360w. ro */
	int controller_present;         /* 360w controller presence. ro */
	int controller_type;            /* controller type. ro */
	char controller_unique_id[17];  /* unique ID of controller (360w). ro */
	unsigned int left_dead_zone;    /* dead zone for left stick. rw */
	unsigned int right_dead_zone;   /* dead zone for right stick. rw */
	unsigned int left_stick_limit;  /* axis limit for left stick. rw */
	unsigned int right_stick_limit; /* axis limit for right stick. rw */
	int rumble_enable;              /* enable/disable rumble. rw */
	int left_trigger_full_axis;     /* full axis - left trigger. rw */
	int right_trigger_full_axis;    /* full axis - right trigger. rw */

	int sysfs_ok;                   /* sysfs interface OK */
};
#define to_xpad(d) input_get_drvdata(to_input_dev(d))


/* Function prototypes for non-sysfs interface functions */
static void set_dead_zone(unsigned int new_size, unsigned int *dz,
	unsigned int stick_limit);
static void set_stick_limit(unsigned int new_size, unsigned int *sl,
	unsigned int dead_zone);
static void xpad_init_controller(struct usb_xpad *xpad);
static void xpad_work_controller(struct work_struct *w);
static void xpad_process_sticks(struct usb_xpad *xpad, __le16 *data);
static void xpad_process_packet(struct usb_xpad *xpad, u16 cmd,
	unsigned char *data);
static void xpad360_process_packet(struct usb_xpad *xpad, u16 cmd,
	unsigned char *data);
static void xpad360w_identify_controller(struct usb_xpad *xpad);
static void xpad360w_process_packet(struct usb_xpad *xpad, u16 cmd,
	unsigned char *data);
static void xpad_irq_in(struct urb *urb);
static void xpad_irq_out(struct urb *urb);
static int xpad_init_output(struct usb_interface *intf, struct usb_xpad *xpad);
static void xpad_stop_output(struct usb_xpad *xpad);
static void xpad_stop_output(struct usb_xpad *xpad);
static int xpad_play_effect(struct input_dev *dev, void *data,
			    struct ff_effect *effect);
static int xpad_init_ff(struct usb_xpad *xpad);
#ifdef CONFIG_JOYSTICK_XPAD_LEDS
static void xpad_send_led_command(struct usb_xpad *xpad, int command);
static void xpad_led_set(struct led_classdev *led_cdev,
	enum led_brightness value);
static int xpad_led_probe(struct usb_xpad *xpad);
static void xpad_led_disconnect(struct usb_xpad *xpad);
#endif
static int xpad_open(struct input_dev *dev);
static void xpad_close(struct input_dev *dev);
static void xpad_set_up_abs(struct input_dev *input_dev, signed short abs);
static int xpad_probe(struct usb_interface *intf,
	const struct usb_device_id *id);
static void xpad_disconnect(struct usb_interface *intf);
static int __init usb_xpad_init(void);
static void __exit usb_xpad_exit(void);


/* sysfs interface */
static ssize_t xpad_show_uint(struct device *dev, struct device_attribute *attr,
		char *buf);
static ssize_t xpad_store_uint(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t xpad_store_bool(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t xpad_store_ro(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count);
static ssize_t xpad_show_int(struct device *dev, struct device_attribute *attr,
		char *buf);
static ssize_t xpad_show_id(struct device *dev,
		struct device_attribute *attr, char *buf);



/* Device attributes */
static DEVICE_ATTR(left_dead_zone, 0644, xpad_show_uint, xpad_store_uint);
static DEVICE_ATTR(right_dead_zone, 0644, xpad_show_uint, xpad_store_uint);
static DEVICE_ATTR(left_stick_limit, 0644, xpad_show_uint, xpad_store_uint);
static DEVICE_ATTR(right_stick_limit, 0644, xpad_show_uint, xpad_store_uint);
static DEVICE_ATTR(rumble_enable, 0644, xpad_show_int, xpad_store_bool);
static DEVICE_ATTR(left_trigger_full_axis, 0644, xpad_show_int,
			xpad_store_bool);
static DEVICE_ATTR(right_trigger_full_axis, 0644, xpad_show_int,
			xpad_store_bool);
static DEVICE_ATTR(controller_number, 0444, xpad_show_int, xpad_store_ro);
static DEVICE_ATTR(controller_present, 0444, xpad_show_int, xpad_store_ro);
static DEVICE_ATTR(controller_type, 0444, xpad_show_int, xpad_store_ro);
static DEVICE_ATTR(id, 0444, xpad_show_id, xpad_store_ro);

static struct attribute *xpad_default_attrs[] = {
	&dev_attr_left_dead_zone.attr,
	&dev_attr_right_dead_zone.attr,
	&dev_attr_left_stick_limit.attr,
	&dev_attr_right_stick_limit.attr,
	&dev_attr_rumble_enable.attr,
	&dev_attr_left_trigger_full_axis.attr,
	&dev_attr_right_trigger_full_axis.attr,
	&dev_attr_controller_number.attr,
	&dev_attr_controller_present.attr,
	&dev_attr_controller_type.attr,
	&dev_attr_id.attr,
	NULL
};

static struct attribute_group xpad_default_attr_group = {
	.attrs = xpad_default_attrs,
	.name = "game_device",
};

#endif

/* Driver History:
 *
 * 2009-03-02 : Code cleanup
 *  - used min(), max(), and abs() where appropriate, simplifying code
 *  - moved code that generates data objects out of xpad.h and into xpad.c
 *  - changed legacy #if defined(...) to #ifdef
 *  - removed unnecessary typecasts
 *  - wireless 360 controller identification now done in workqueue task
 *  - thanks Andrew Morton, Greg K-H, and Linus Torvalds
 *
 * 2009-02-28 : Triggers now half-axes by default
 *  - triggers will now be positive half-axes only, unless a full axis mapping
 *    is enabled via the sysfs interface on a per-trigger basis
 *  - moved INIT_WORK to xpad_probe and removed INIT_WORK/PREPARE_WORK from
 *    interrupt handler; also removed the work_pending flag from struct
 *    usb_xpad (always flush shared workqueue on unload)
 *  - read-write sysfs attributes now have 644 default permissions
 *
 * 2009-02-23 : Changes per mailing list (thanks Frederic Weisbecker)
 *  - no more check for CONFIG_SYSFS: sysfs functions will simply return
 *    0 if sysfs has not been enabled
 *  - fixed weird ordering in sscanf return check
 *  - checked code with scripts/checkpatch.pl and made style adjustments
 *
 * 2009-02-21 : Refactored and changed stick handling
 *  - split code into two pieces (xpad.h and xpad.c)
 *  - cleaned up sysfs interface
 *  - changed square axis algorithm to an axis limit algorithm, which allows
 *    size of inscribed square to be adjusted; available for both sticks
 *  - dead zones now per-stick
 *
 * 2009-02-18 : Changes per mailing list (and some additions)
 *  - revised sysfs interface (thanks Greg K-H)
 *  - check return values of sscanf (thanks Oliver Neukum)
 *  - urb submission while holding mutex now once again GFP_KERNEL
 *    (thanks Oliver Neukum)
 *  - work structure fixes (thanks Oliver Neukum)
 *  - uevents generated for wireless controller online/offline
 *  - sysfs interface only if CONFIG_SYSFS is set
 *
 * 2009-02-15 : Minor adjustments
 *  - added KOBJ_ONLINE/KOBJ_OFFLINE events when controllers are connected to
 *    or disconnected from the wireless 360 receiver
 *  - ignore duplicate connect messages on the same connection
 *  - added option to enable/disable rumble on a per-controller basis
 *  - rumble events are not sent to guitar or dance pad devices
 *
 * 2009-02-14 : Added sysfs interface
 *  - dead zones and square axis settings can now be made per-controller
 *  - removed dead_zone and square_axis module parameters (use sysfs)
 *  - new square axis algorithm
 *
 * 2009-02-13 : Disable square axis for right stick
 *  - square axis applies to left stick only
 *
 * 2009-02-12 : Scaling for dead zone and square axis support
 *  - axes now scale from 0 to 32767 starting at edge of dead zone
 *  - increased default dead zone to 8192
 *  - initial square axis support (reliable only with left stick)
 *
 * 2009-02-07 : More wireless 360 controller fixes
 *  - removed bulk urb completely
 *  - use xpad_send_led_command to set controller number on LED display
 *    (wireless 360 controller)
 *  - dead_zone is now an adjustable module parameter
 *
 * 2009-02-06 : Axis handling improvements
 *  - unified handler for left and right sticks
 *  - initial support for dead zones
 *
 * 2009-02-02 : Wireless 360 controller fixes
 *  - followed PROTOCOL description from xboxdrv userspace driver
 *  - LED and rumble support added for wireless 360 controller (protocol
 *    is different from wired!)
 *
 * 2004-10-02 - 0.0.6 : DDR pad support
 *  - borrowed from the XBOX linux kernel
 *  - USB id's for commonly used dance pads are present
 *  - dance pads will map D-PAD to buttons, not axes
 *  - pass the module paramater 'dpad_to_buttons' to force
 *    the D-PAD to map to buttons if your pad is not detected
 *
 * 2002-07-17 - 0.0.5 : simplified d-pad handling
 *
 * 2002-07-16 - 0.0.4 : minor changes, merge with Vojtech's v0.0.3
 *  - verified the lack of HID and report descriptors
 *  - verified that ALL buttons WORK
 *  - fixed d-pad to axes mapping
 *
 * 2002-07-14 - 0.0.3 : rework by Vojtech Pavlik
 *  - indentation fixes
 *  - usb + input init sequence fixes
 *
 * 2002-07-02 - 0.0.2 : basic working version
 *  - all axes and 9 of the 10 buttons work (german InterAct device)
 *  - the black button does not work
 *
 * 2002-06-27 - 0.0.1 : first version, just said "XBOX HID controller"
 *
 */
