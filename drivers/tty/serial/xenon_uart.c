/* linux/drivers/serial/xenon.c
 *
 * Driver for Xenon XBOX 360 Serial
 *
 * Copyright (C) 2010 Herbert Poetzl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/io.h>

#if 0
#define	dprintk(f, x...) do {				\
		printk(KERN_DEBUG f "\n" , ##x);	\
	} while (0)
#else
#define dprintk(f, x...) do { } while (0)
#endif


static int xenon_status(unsigned char __iomem *membase)
{
	// return ((*(volatile uint32_t*)0x80000200ea001018) & 0x02000000);
	return (*(volatile uint32_t*)(membase + 0x08));
}

static void xenon_putch(unsigned char __iomem *membase, int ch)
{
	/* wait for tx fifo ready */
	while (!(xenon_status(membase) & 0x02000000));

	/* put character into fifo */
	// *(volatile uint32_t*)0x80000200ea001014 = (ch << 24) & 0xFF000000;
	*(volatile uint32_t*)(membase + 0x04) = (ch << 24) & 0xFF000000;
}

static int xenon_getch(unsigned char __iomem *membase)
{
	uint32_t status;

	/* wait for data ready */
	while ((status = xenon_status(membase)) & ~0x03000000);

	if (status & 0x01000000)
		return *(volatile uint32_t*)(membase + 0x00) >> 24;
	return -1;
}



static void xenon_stop_rx(struct uart_port *port)
{
	dprintk("Xenon xenon_stop_rx()");
}

static void xenon_enable_ms(struct uart_port *port)
{
	dprintk("Xenon xenon_enable_ms()");
}

static void xenon_stop_tx(struct uart_port *port)
{
	dprintk("Xenon xenon_stop_tx()");
}

static void xenon_tx_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	// int count;

	if (port->x_char) {
		xenon_putch(port->membase, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		xenon_stop_tx(port);
		return;
	}

#if 0
	count = port->fifosize >> 1;
	do {
		xenon_putch(port->membase, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (uart_circ_empty(xmit))
			break;
	} while (--count > 0);
#else
	while (!uart_circ_empty(xmit)) {
		xenon_putch(port->membase, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
	}
#endif

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		xenon_stop_tx(port);
}

static void xenon_start_tx(struct uart_port *port)
{
	dprintk("Xenon xenon_start_tx()");
	xenon_tx_chars(port);
}


#if 0
static void xenon_send_xchar(struct uart_port *port, char ch)
{
	dprintk("Xenon xenon_send_xchar(%d)", ch);
	xenon_putch(port->membase, ch);
}
#endif

static unsigned int xenon_tx_empty(struct uart_port *port)
{
	dprintk("Xenon xenon_tx_empty()");
	return 0;
}

static unsigned int xenon_get_mctrl(struct uart_port *port)
{
	dprintk("Xenon xenon_get_mctrl()");
	return 0;
}

static void xenon_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	dprintk("Xenon xenon_set_mctrl()");
}

static void xenon_break_ctl(struct uart_port *port, int break_state)
{
	dprintk("Xenon xenon_break_ctl()");
}

static void xenon_set_termios(struct uart_port *port,
			     struct ktermios *new, struct ktermios *old)
{
	int baud, quot, cflag = new->c_cflag;

	dprintk("Xenon xenon_set_termios()");
	/* get the byte size */
	switch (cflag & CSIZE) {
	case CS5:
		dprintk(" - data bits = 5");
		break;
	case CS6:
		dprintk(" - data bits = 6");
		break;
	case CS7:
		dprintk(" - data bits = 7");
		break;
	default: // CS8
		dprintk(" - data bits = 8");
		break;
	}

	/* determine the parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			pr_debug(" - parity = odd\n");
		else
			pr_debug(" - parity = even\n");
	else
		pr_debug(" - parity = none\n");

	/* figure out the stop bits requested */
	if (cflag & CSTOPB)
		pr_debug(" - stop bits = 2\n");
	else
		pr_debug(" - stop bits = 1\n");

	/* figure out the flow control settings */
	if (cflag & CRTSCTS)
		pr_debug(" - RTS/CTS is enabled\n");
	else
		pr_debug(" - RTS/CTS is disabled\n");

	/* Set baud rate */
	baud = uart_get_baud_rate(port, new, old, 0, port->uartclk/16);
	quot = uart_get_divisor(port, baud);
}

static int xenon_startup(struct uart_port *port)
{
	dprintk("Xenon xenon_startup()");
	/* this is the first time this port is opened */
	/* do any hardware initialization needed here */
	return 0;
}

static void xenon_shutdown(struct uart_port *port)
{
	dprintk("Xenon xenon_shutdown()");
	/* The port is being closed by the last user. */
	/* Do any hardware specific stuff here */
}

static const char *xenon_type(struct uart_port *port)
{
	return "Xenon SMC";
}

static void xenon_release_port(struct uart_port *port)
{
	dprintk("Xenon xenon_release_port()");
}

static int xenon_request_port(struct uart_port *port)
{
	dprintk("Xenon xenon_request_port()");
	return 0;
}

static void xenon_config_port(struct uart_port *port, int flags)
{
	dprintk("Xenon xenon_config_port()");

	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_XENON;
	}
}

static int xenon_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	dprintk("Xenon xenon_verify_port()");
	return 0;
}


#ifdef CONFIG_CONSOLE_POLL

static int xenon_poll_get_char(struct uart_port *port)
{
	return xenon_getch(port->membase);
}

static void xenon_poll_put_char(struct uart_port *port, unsigned char c)
{
	xenon_putch(port->membase, c);
}

#endif


static struct uart_ops xenon_ops = {
	.tx_empty	= xenon_tx_empty,
	.set_mctrl	= xenon_set_mctrl,
	.get_mctrl	= xenon_get_mctrl,
	.stop_tx	= xenon_stop_tx,
	.start_tx	= xenon_start_tx,
//	.send_xchar	= xenon_send_xchar,
	.stop_rx	= xenon_stop_rx,
	.enable_ms	= xenon_enable_ms,
	.break_ctl	= xenon_break_ctl,
	.startup	= xenon_startup,
	.shutdown	= xenon_shutdown,
//	.flush_buffer	= xenon_flush_buffer,
	.set_termios	= xenon_set_termios,
//	.set_ldisc	= xenon_set_ldisc,
//	.pm		= xenon_pm,
//	.set_wake	= xenon_set_wake,
	.type		= xenon_type,
	.release_port	= xenon_release_port,
	.request_port	= xenon_request_port,
	.config_port	= xenon_config_port,
	.verify_port	= xenon_verify_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char	= xenon_poll_put_char,
	.poll_get_char	= xenon_poll_get_char,
#endif
};

static struct uart_port xenon_port = {
	.type		= PORT_XENON,
	.ops		= &xenon_ops,
	.flags		= UPF_FIXED_TYPE | UPF_IOREMAP,
	.mapbase	= 0x200ea001010ULL,
	.iotype         = UPIO_MEM,
	.uartclk	= 1843200,
};

static struct console xenon_console;

static struct uart_driver xenon_reg = {
	.owner  	= THIS_MODULE,
	.driver_name	= "xenon_uart",
	.dev_name	= "ttyS",
	.major  	= TTY_MAJOR,
	.minor  	= 64,
	.nr		= 1,
#ifdef	CONFIG_SERIAL_XENON_CONSOLE
	.cons		= &xenon_console,
#endif
};


static int __init xenon_init(void)
{
	int result;

	printk(KERN_INFO "Xenon XBOX 360 serial driver\n");

	result = uart_register_driver(&xenon_reg);
	dprintk("Xenon uart_register_driver() = %d", result);
	if (result)
		return result;

	xenon_port.membase = ioremap_nocache(xenon_port.mapbase, 0x10);

	result = uart_add_one_port(&xenon_reg, &xenon_port);
	dprintk("Xenon uart_add_one_port() = %d", result);
	if (result)
		uart_unregister_driver(&xenon_reg);

	return result;
}

static void __exit xenon_exit(void)
{
	printk(KERN_INFO "Xenon XBOX 360 serial driver exit\n");
	uart_remove_one_port(&xenon_reg, &xenon_port);
	uart_unregister_driver(&xenon_reg);
}

module_init(xenon_init);
module_exit(xenon_exit);


#ifdef CONFIG_SERIAL_XENON_CONSOLE

static void xenon_console_putchar(struct uart_port *port, int ch)
{
	xenon_putch(port->membase, ch);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 */
static void xenon_console_write(struct console *cons,
	const char *s, unsigned int count)
{
	uart_console_write(&xenon_port, s, count, xenon_console_putchar);
}

/*
 * Setup serial console baud/bits/parity.  We do two things here:
 * - construct a cflag setting for the first uart_open()
 * - initialise the serial port
 * Return non-zero if we didn't find a serial port.
 */
static int __init xenon_console_setup(struct console *cons, char *options)
{
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
#if 0
	ret = xenon_map_port(uport);
	if (ret)
		return ret;

	xenon_reset(port);
	xenon_pm(port, 0, -1);
#endif

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	return uart_set_options(&xenon_port, cons, baud, parity, bits, flow);
}

static struct console xenon_console = {
	.name	= "ttyS",
	.write	= xenon_console_write,
	.device	= uart_console_device,
	.setup	= xenon_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &xenon_reg,
};

static int __init xenon_serial_console_init(void)
{
	xenon_port.membase = ioremap_nocache(xenon_port.mapbase, 0x10);

	register_console(&xenon_console);
	return 0;
}

console_initcall(xenon_serial_console_init);

#endif /* CONFIG_SERIAL_XENON_CONSOLE */


MODULE_AUTHOR("Herbert Poetzl <herbert@13thfloor.at>");
MODULE_DESCRIPTION("Xenon XBOX 360 Serial port driver");
MODULE_LICENSE("GPL v2");
// MODULE_ALIAS("platform:xenon-uart");
