#include <linux/module.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#if defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#include <linux/sysrq.h>
#endif
#include <linux/serial_core.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <mach/sp_uart.h>

#define NUM_UART	6

/* ---------------------------------------------------------------------------------------------- */
#define TTYS_KDBG_INFO
#define TTYS_KDBG_ERR

#ifdef TTYS_KDBG_INFO
	#define DBG_INFO(fmt, args ...)	printk(KERN_INFO "K_TTYS: " fmt, ## args)
#else
	#define DBG_INFO(fmt, args ...)
#endif

#ifdef TTYS_KDBG_ERR
	#define DBG_ERR(fmt, args ...)	printk(KERN_ERR "K_TTYS: " fmt, ## args)
#else
	#define DBG_ERR(fmt, args ...)
#endif
/* ---------------------------------------------------------------------------------------------- */
#define DEVICE_NAME			"ttyS"
#define SP_UART_MAJOR			TTY_MAJOR
#define SP_UART_MINOR_START		64

#define SP_UART_CREAD_DISABLED	(1 << 16)
/* ---------------------------------------------------------------------------------------------- */
#define UARXDMA0			(-1)	/* Which port to use UARXDMA0, (-1) => disabled */
#define UARXDMA1			(-1)	/* Which port to use UARXDMA1, (-1) => disabled */

#if ((UARXDMA0 != -1) || (UARXDMA1 != -1))
#define ENABLE_UARXDMA
#endif

#define UARXDMA0_BUF_SZ			PAGE_SIZE
#define UARXDMA1_BUF_SZ			PAGE_SIZE
#define MAX_SZ_RXDMA_ISR		(1 << 9)
/* ---------------------------------------------------------------------------------------------- */

struct uarxdma_info {
	struct regs_uarxdma *rxdma_reg;
	unsigned int irq_num;
	struct uart_port *port;
	irq_handler_t handler;
	char *irq_name;
	unsigned int buf_sz;
	void *addr_va;
	void *addr_pa;
};

struct uart_hw_binding {
	struct uarxdma_info *uarxdma;
};

#ifdef ENABLE_UARXDMA
static irqreturn_t sunplus_uart_rxdma0_irq(int irq, void *args);	/* forward declaration */
static irqreturn_t sunplus_uart_rxdma1_irq(int irq, void *args);	/* forward declaration */
static struct uarxdma_info uarxdma[] = {
	{
		.rxdma_reg = ((struct regs_uarxdma *)(LOGI_ADDR_UADMA0_REG)),
		.irq_num   = SP_IRQ_DMA0,		/* (SP_IRQ_GIC_START + 138) */
		.port      = NULL,			/* assigned after binding to UARTx */
		.handler   = sunplus_uart_rxdma0_irq,
		.irq_name  = "UARXDMA0",
		.buf_sz    = UARXDMA0_BUF_SZ,
		.addr_va   = NULL,			/* allocated after first time of executing .startup() */
		.addr_pa   = NULL,
	},
	{
		.rxdma_reg = ((struct regs_uarxdma *)(LOGI_ADDR_UADMA1_REG)),
		.irq_num   = SP_IRQ_DMA1,		/* (SP_IRQ_GIC_START + 139) */
		.port      = NULL,			/* assigned after binding to UARTx */
		.handler   = sunplus_uart_rxdma1_irq,
		.irq_name  = "UARXDMA1",
		.buf_sz    = UARXDMA1_BUF_SZ,
		.addr_va   = NULL,			/* allocated after first time of executing .startup() */
		.addr_pa   = NULL,
	},
};

static struct uart_hw_binding sp_uart[NUM_UART];

#endif /* ENABLE_UARXDMA */

#if defined(CONFIG_MAGIC_SYSRQ)
extern unsigned int uart0_mask_tx;	/* Used for masking uart0 tx output */
#endif

static inline void sp_uart_set_int_en(unsigned char __iomem *base, unsigned int_state)
{
	writel(int_state, &((struct regs_uart *)base)->uart_isc);
}

static inline unsigned sp_uart_get_int_en(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_isc);
}

static inline int sp_uart_get_char(unsigned char __iomem *base)
{
	return readl_relaxed(&((struct regs_uart *)base)->uart_data);
}

static inline void sp_uart_put_char(unsigned char __iomem *base, unsigned ch)
{
#if defined(CONFIG_MAGIC_SYSRQ)
	if (likely(!((uart0_mask_tx == 1) && (base == LOGI_ADDR_UART0_REG))))
		writel_relaxed(ch,  &((struct regs_uart *)base)->uart_data);
#else
	writel_relaxed(ch,  &((struct regs_uart *)base)->uart_data);
#endif
}

static inline unsigned sp_uart_get_line_status(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_lsr);
}

static inline void sp_uart_set_line_ctrl(unsigned char __iomem *base, unsigned ctrl)
{
	writel(ctrl, &((struct regs_uart *)base)->uart_lcr);
}

static inline unsigned sp_uart_get_line_ctrl(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_lcr);
}

static inline void sp_uart_set_divider_low_register(unsigned char __iomem *base, unsigned val)
{
	writel(val, &((struct regs_uart *)base)->uart_div_l);
}

static inline unsigned sp_uart_get_divider_low_register(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_div_l);
}

static inline void sp_uart_set_divider_high_register(unsigned char __iomem *base, unsigned val)
{
	writel(val, &((struct regs_uart *)base)->uart_div_h);
}

static inline unsigned sp_uart_get_divider_high_register(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_div_h);
}

static inline void sp_uart_set_rx_residue(unsigned char __iomem *base, unsigned val)
{
	writel(val, &((struct regs_uart *)base)->uart_rx_residue);
}

static inline void sp_uart_set_modem_ctrl(unsigned char __iomem *base, unsigned val)
{
	writel(val, &((struct regs_uart *)base)->uart_mcr);
}

static inline unsigned sp_uart_get_modem_ctrl(unsigned char __iomem *base)
{
	return readl(&((struct regs_uart *)base)->uart_mcr);
}

/* ---------------------------------------------------------------------------------------------- */

/*
 * Note:
 * When (uart0_as_console == 0), please make sure:
 *     There is no "console=ttyS0,115200", "earlyprintk", ... in kernel command line.
 *     In /etc/inittab, there is no something like "ttyS0::respawn:/bin/sh"
 */
unsigned int uart0_as_console = ~0;
unsigned int uart_enable_status = 0;	/* bit 0: UART0, bit 1: UART1, ... */

#if defined(CONFIG_MAGIC_SYSRQ)
extern int sysrqCheckState(char, struct uart_port *);
#endif

struct sunplus_uart_port {
	char name[16];	/* Sunplus_UARTx */
	struct uart_port uport;
	struct uarxdma_info *uarxdma;
};

struct sunplus_uart_port sunplus_uart_ports[NUM_UART];

static inline void wait_for_xmitr(struct uart_port *port)
{
	unsigned int status;

	do {
		status = sp_uart_get_line_status(port->membase);
	} while (!(status & SP_UART_LSR_TX));
}

static void sunplus_uart_console_putchar(struct uart_port *port, int ch)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port->membase, ch);
}

static void sunplus_console_write(struct console *co, const char *s, unsigned count)
{
	unsigned long flags;
	int locked = 1;

	local_irq_save(flags);

#if defined(CONFIG_MAGIC_SYSRQ)
	if (sunplus_uart_ports[co->index].uport.sysrq)
#else
	if (0)
#endif
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock(&sunplus_uart_ports[co->index].uport.lock);
	else
		spin_lock(&sunplus_uart_ports[co->index].uport.lock);

	uart_console_write(&sunplus_uart_ports[co->index].uport, s, count, sunplus_uart_console_putchar);

	if (locked)
		spin_unlock(&sunplus_uart_ports[co->index].uport.lock);

	local_irq_restore(flags);
}

static int __init sunplus_console_setup(struct console *co, char *options)
{
	int ret = 0;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	/* char string_console_setup[] = "\n\nsunplus_console_setup()\n\n"; */

	if ((co->index >= NUM_UART) || (co->index < 0))
		return -EINVAL;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(&sunplus_uart_ports[co->index].uport, co, baud, parity, bits, flow);
	/* sunplus_console_write(co, string_console_setup, sizeof(string_console_setup)); */
	return ret;
}

/*
 * Documentation/serial/driver:
 * tx_empty(port)
 * This function tests whether the transmitter fifo and shifter
 * for the port described by 'port' is empty.  If it is empty,
 * this function should return TIOCSER_TEMT, otherwise return 0.
 * If the port does not support this operation, then it should
 * return TIOCSER_TEMT.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 * This call must not sleep
 */
static unsigned int sunplus_uart_ops_tx_empty(struct uart_port *port)
{
	return ((sp_uart_get_line_status(port->membase) & SP_UART_LSR_TXE) ? TIOCSER_TEMT : 0);
}

/*
 * Documentation/serial/driver:
 * set_mctrl(port, mctrl)
 * This function sets the modem control lines for port described
 * by 'port' to the state described by mctrl.  The relevant bits
 * of mctrl are:
 *     - TIOCM_RTS     RTS signal.
 *     - TIOCM_DTR     DTR signal.
 *     - TIOCM_OUT1    OUT1 signal.
 *     - TIOCM_OUT2    OUT2 signal.
 *     - TIOCM_LOOP    Set the port into loopback mode.
 * If the appropriate bit is set, the signal should be driven
 * active.  If the bit is clear, the signal should be driven
 * inactive.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static void sunplus_uart_ops_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	unsigned char mcr = sp_uart_get_modem_ctrl(port->membase);

	if (mctrl & TIOCM_DTR)
		mcr |= SP_UART_MCR_DTS;
	else
		mcr &= ~SP_UART_MCR_DTS;

	if (mctrl & TIOCM_RTS)
		mcr |= SP_UART_MCR_RTS;
	else
		mcr &= ~SP_UART_MCR_RTS;

	if (mctrl & TIOCM_CAR)
		mcr |= SP_UART_MCR_DCD;
	else
		mcr &= ~SP_UART_MCR_DCD;

	if (mctrl & TIOCM_RI)
		mcr |= SP_UART_MCR_RI;
	else
		mcr &= ~SP_UART_MCR_RI;

	if (mctrl & TIOCM_LOOP)
		mcr |= SP_UART_MCR_LB;
	else
		mcr &= ~SP_UART_MCR_LB;

	sp_uart_set_modem_ctrl(port->membase, mcr);
}

/*
 * Documentation/serial/driver:
 * get_mctrl(port)
 * Returns the current state of modem control inputs.  The state
 * of the outputs should not be returned, since the core keeps
 * track of their state.  The state information should include:
 *     - TIOCM_CAR     state of DCD signal
 *     - TIOCM_CTS     state of CTS signal
 *     - TIOCM_DSR     state of DSR signal
 *     - TIOCM_RI      state of RI signal
 * The bit is set if the signal is currently driven active.  If
 * the port does not support CTS, DCD or DSR, the driver should
 * indicate that the signal is permanently active.  If RI is
 * not available, the signal should not be indicated as active.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static unsigned int sunplus_uart_ops_get_mctrl(struct uart_port *port)
{
	unsigned char status;
	unsigned int ret = 0;

	status = sp_uart_get_modem_ctrl(port->membase);

	if (status & SP_UART_MCR_DTS)
		ret |= TIOCM_DTR;

	if (status & SP_UART_MCR_RTS)
		ret |= TIOCM_RTS;

	if (status & SP_UART_MCR_DCD)
		ret |= TIOCM_CAR;

	if (status & SP_UART_MCR_RI)
		ret |= TIOCM_RI;

	if (status & SP_UART_MCR_LB)
		ret |= TIOCM_LOOP;

	if (status & SP_UART_MCR_AC)
		ret |= TIOCM_CTS;

	return ret;
}

/*
 * Documentation/serial/driver:
 * stop_tx(port)
 * Stop transmitting characters.  This might be due to the CTS
 * line becoming inactive or the tty layer indicating we want
 * to stop transmission due to an XOFF character.
 *
 * The driver should stop transmitting characters as soon as
 * possible.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static void sunplus_uart_ops_stop_tx(struct uart_port *port)
{
	unsigned int isc = sp_uart_get_int_en(port->membase);
	isc &= ~SP_UART_ISC_TXM;
	sp_uart_set_int_en(port->membase, isc);
}

/*
 * Documentation/serial/driver:
 * start_tx(port)
 * Start transmitting characters.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static void sunplus_uart_ops_start_tx(struct uart_port *port)
{
	unsigned int isc;

	isc = sp_uart_get_int_en(port->membase) | SP_UART_ISC_TXM;
	sp_uart_set_int_en(port->membase, isc);
}

/*
 * Documentation/serial/driver:
 * send_xchar(port,ch)
 * Transmit a high priority character, even if the port is stopped.
 * This is used to implement XON/XOFF flow control and tcflow().  If
 * the serial driver does not implement this function, the tty core
 * will append the character to the circular buffer and then call
 * start_tx() / stop_tx() to flush the data out.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
#if 0
static void sunplus_uart_ops_send_xchar(struct uart_port *port, char ch)
{
}
#endif

/*
 * Documentation/serial/driver:
 * stop_rx(port)
 * Stop receiving characters; the port is in the process of
 * being closed.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static void sunplus_uart_ops_stop_rx(struct uart_port *port)
{
	unsigned int isc;

	isc = sp_uart_get_int_en(port->membase);
	isc &= ~SP_UART_ISC_RXM;
	sp_uart_set_int_en(port->membase, isc);
}

/*
 * Documentation/serial/driver:
 *
 * enable_ms(port)
 * Enable the modem status interrupts.
 *
 * This method may be called multiple times.  Modem status
 * interrupts should be disabled when the shutdown method is
 * called.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 */
static void sunplus_uart_ops_enable_ms(struct uart_port *port)
{
	/* Do nothing */
}

/*
 * Documentation/serial/driver:
 * break_ctl(port,ctl)
 * Control the transmission of a break signal.  If ctl is
 * nonzero, the break signal should be transmitted.  The signal
 * should be terminated when another call is made with a zero
 * ctl.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 * This call must not sleep
 */
static void sunplus_uart_ops_break_ctl(struct uart_port *port, int ctl)
{
	unsigned long flags;
	unsigned int h_lcr;

	spin_lock_irqsave(&port->lock, flags);

	h_lcr = sp_uart_get_line_ctrl(port->membase);

	if (ctl != 0)
		h_lcr |= SP_UART_LCR_BC;	/* start break */
	else
		h_lcr &= ~SP_UART_LCR_BC;	/* stop break */

	sp_uart_set_line_ctrl(port->membase, h_lcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

static void transmit_chars(struct uart_port *port)	/* called by ISR */
{
	struct circ_buf *xmit = &port->state->xmit;

	if (port->x_char) {
		sp_uart_put_char(port->membase, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		return;
	}

	if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
		sunplus_uart_ops_stop_tx(port);
		return;
	}

	do {
		sp_uart_put_char(port->membase, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;

		if (uart_circ_empty(xmit))
			break;
	} while ((sp_uart_get_line_status(port->membase) & (SP_UART_LSR_TX)) != 0); /* transmit FIFO is not full */

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		sunplus_uart_ops_stop_tx(port);
}

static void receive_chars(struct uart_port *port)	/* called by ISR */
{
	struct tty_struct *tty = port->state->port.tty;
	unsigned char lsr = sp_uart_get_line_status(port->membase);
	unsigned int ch, flag;

	do {
		ch = sp_uart_get_char(port->membase);

#if defined(CONFIG_MAGIC_SYSRQ)
		if ( sysrqCheckState(ch, port) != 0)
			goto ignore_char;
#endif

		flag = TTY_NORMAL;
		port->icount.rx++;

		if (unlikely(lsr & SP_UART_LSR_BRK_ERROR_BITS)) {
			if (port->cons == NULL)
				DBG_ERR("UART%d, SP_UART_LSR_BRK_ERROR_BITS, lsr = 0x%08X\n", port->line, lsr);

			if (lsr & SP_UART_LSR_BC) {
				lsr &= ~(SP_UART_LSR_FE | SP_UART_LSR_PE);
				port->icount.brk++;
				if (uart_handle_break(port))
					goto ignore_char;
			} else if (lsr & SP_UART_LSR_PE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_PE\n", port->line);
				port->icount.parity++;
			} else if (lsr & SP_UART_LSR_FE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_FE\n", port->line);
				port->icount.frame++;
			}
			if (lsr & SP_UART_LSR_OE) {
				if (port->cons == NULL)
					DBG_ERR("UART%d, SP_UART_LSR_OE\n", port->line);
				port->icount.overrun++;
			}

			/*
			 * Mask off conditions which should be ignored.
			 */

			/* lsr &= port->read_status_mask; */

			if (lsr & SP_UART_LSR_BC)
				flag = TTY_BREAK;
			else if (lsr & SP_UART_LSR_PE)
				flag = TTY_PARITY;
			else if (lsr & SP_UART_LSR_FE)
				flag = TTY_FRAME;
		}

		if (port->ignore_status_mask & SP_UART_CREAD_DISABLED) {
			goto ignore_char;
		}

		if (uart_handle_sysrq_char(port, ch))
			goto ignore_char;

		uart_insert_char(port, lsr, SP_UART_LSR_OE, ch, flag);

ignore_char:
		lsr = sp_uart_get_line_status(port->membase);
	} while (lsr & SP_UART_LSR_RX);

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tty->port);
	spin_lock(&port->lock);
}

static irqreturn_t sunplus_uart_irq(int irq, void *args)
{
	struct uart_port *port = (struct uart_port *)args;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	if (sp_uart_get_int_en(port->membase) & SP_UART_ISC_RX)
		receive_chars(port);

	if (sp_uart_get_int_en(port->membase) & SP_UART_ISC_TX)
		transmit_chars(port);

	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

#ifdef ENABLE_UARXDMA
static void receive_chars_rxdma(struct uart_port *port)	/* called by ISR */
{
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);
	struct uarxdma_info *uarxdma =
		(struct uarxdma_info *)(sp_port->uarxdma);
	struct regs_uarxdma *rxdma_reg =
		(struct regs_uarxdma *)(sp_port->uarxdma->rxdma_reg);
	struct tty_struct *tty = port->state->port.tty;
	u32 offset_sw, offset_hw, rx_size;
	u8 *sw_ptr, *buf_end_ptr, *u8_ptr;
	u32 icount_rx;
	u32 tmp_u32;
	u8 tmp_buf[32];
	int i;

	rx_size = rxdma_reg->rxdma_databytes;
	offset_sw = rxdma_reg->rxdma_rd_adr - rxdma_reg->rxdma_start_addr;
	offset_hw = rxdma_reg->rxdma_wr_adr - rxdma_reg->rxdma_start_addr;

	if (offset_hw >= offset_sw) {
		if (rx_size != offset_hw - offset_sw) {
			DBG_ERR("\n%s, %d\n\n", __FILE__, __LINE__);
			BUG_ON(1);
		}
	} else {
		if (rx_size != (offset_hw + uarxdma->buf_sz - offset_sw)) {
			DBG_ERR("\n%s, %d\n\n", __FILE__, __LINE__);
			BUG_ON(1);
		}
	}

	sw_ptr = (u8 *)(uarxdma->addr_va + offset_sw);
	buf_end_ptr = (u8 *)(uarxdma->addr_va + uarxdma->buf_sz);

	/*
	 * Retrive all data in ISR.
	 * The max. received size is (buffer_size - threshold_size)
	 * = (rxdma_length_thr[31:16] - rxdma_length_thr[15:0]) = MAX_SZ_RXDMA_ISR
	 * In order to limit the execution time in this ISR:
	 * => Increase rxdma_length_thr[15:0] to shorten each ISR execution time.
	 * => Don't need to set a small threshold_size,
	 *    and split a long ISR into several shorter ISRs.
	 */
	icount_rx = 0;
	while (rx_size > icount_rx) {
		if (!(((u32)(sw_ptr)) & (32 - 1))	/* 32-byte aligned */
		    && ((rx_size - icount_rx) >= 32)) {
			/*
			 * Copy 32 bytes data from non cache area to cache area.
			 * => It should use less DRAM controller's read command.
			 */
			memcpy(tmp_buf, sw_ptr, 32);
			u8_ptr = (u8 *)(tmp_buf);
			for (i = 0; i < 32; i++) {
				port->icount.rx++;
				uart_insert_char(port, 0, SP_UART_LSR_OE, (unsigned int)(*u8_ptr), TTY_NORMAL);
				u8_ptr++;
			}
			sw_ptr += 32;
			icount_rx +=32;
		} else {
			port->icount.rx++;
			uart_insert_char(port, 0, SP_UART_LSR_OE, (unsigned int)(*sw_ptr), TTY_NORMAL);
			sw_ptr++;
			icount_rx++;
		}
		if (sw_ptr >= buf_end_ptr)
			sw_ptr = (u8 *)(uarxdma->addr_va);
	}
	tmp_u32 = rxdma_reg->rxdma_rd_adr + rx_size;
	if (tmp_u32 <= rxdma_reg->rxdma_end_addr)
		rxdma_reg->rxdma_rd_adr = tmp_u32;
	else
		rxdma_reg->rxdma_rd_adr = tmp_u32 - uarxdma->buf_sz;

	spin_unlock(&port->lock);
	tty_flip_buffer_push(tty->port);
	spin_lock(&port->lock);

	rxdma_reg->rxdma_enable_sel |= DMA_INT;
	rxdma_reg->rxdma_enable_sel |= DMA_GO;
}

static irqreturn_t sunplus_uart_rxdma_irq_common(int irq, void *args)
{
	struct uart_port *port = (struct uart_port *)args;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	receive_chars_rxdma(port);
	spin_unlock_irqrestore(&port->lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t sunplus_uart_rxdma0_irq(int irq, void *args)
{
	return sunplus_uart_rxdma_irq_common(irq, args);
}

static irqreturn_t sunplus_uart_rxdma1_irq(int irq, void *args)
{
	return sunplus_uart_rxdma_irq_common(irq, args);
}
#endif /* ENABLE_UARXDMA */

/*
 * Documentation/serial/driver:
 * startup(port)
 * Grab any interrupt resources and initialise any low level driver
 * state.  Enable the port for reception.  It should not activate
 * RTS nor DTR; this will be done via a separate call to set_mctrl.
 *
 * This method will only be called when the port is initially opened.
 *
 * Locking: port_sem taken.
 * Interrupts: globally disabled.
 */
static int sunplus_uart_ops_startup(struct uart_port *port)
{
	int ret;
	struct sunplus_uart_port *sp_port =
		(struct sunplus_uart_port *)(port->private_data);

#ifdef ENABLE_UARXDMA
	struct uarxdma_info *uarxdma =
		(struct uarxdma_info *)(sp_port->uarxdma);
	struct regs_uarxdma *rxdma_reg;
#endif /* ENABLE_UARXDMA */

	ret = request_irq(port->irq, sunplus_uart_irq, 0, sp_port->name, port);
	if (ret)
		return ret;

#ifdef ENABLE_UARXDMA
	if (uarxdma) {
		rxdma_reg = (struct regs_uarxdma *)(sp_port->uarxdma->rxdma_reg);

		DBG_INFO("Enalbe RXDMA for %s (irq=%d)\n", sp_port->name, (int)(uarxdma->irq_num));
		ret = request_irq(uarxdma->irq_num, uarxdma->handler, 0,
				  uarxdma->irq_name, port);
		if (ret)
			return ret;

		if (uarxdma->addr_va == NULL) {
			/* Allocate buffer and keep it forever */
			uarxdma->addr_va = sp_chunk_malloc_nocache(0, 0, uarxdma->buf_sz);
			if (uarxdma->addr_va == NULL)
				DBG_ERR("Can't allocate buffer for %s\n", uarxdma->irq_name);

			uarxdma->addr_pa = (void *)(sp_chunk_pa(uarxdma->addr_va));
			DBG_INFO("%s: va: 0x%08X, pa:0x%08X\n", __func__,
				 (u32)(uarxdma->addr_va), (u32)(uarxdma->addr_pa));

			rxdma_reg->rxdma_start_addr  = (u32)(uarxdma->addr_pa);
			rxdma_reg->rxdma_rd_adr      = (u32)(uarxdma->addr_pa);
			rxdma_reg->rxdma_timeout_set = 0x0001A5E0;
			/* 0x0001A5E0 * 2 / 270000000 = 800 usec */

			/*
			 * When there are only rxdma_length_thr[15:0] bytes of free buffer
			 * => Trigger interrupt
			 */
			rxdma_reg->rxdma_length_thr = (uarxdma->buf_sz << 16)
						      | (uarxdma->buf_sz - MAX_SZ_RXDMA_ISR);

			rxdma_reg->rxdma_enable_sel &= ~DMA_SEL_UARTX_MASK;
			rxdma_reg->rxdma_enable_sel |= DMA_INIT
						       | (port->line << DMA_SEL_UARTX_SHIFT);
			rxdma_reg->rxdma_enable_sel &= ~DMA_INIT;
			rxdma_reg->rxdma_enable_sel |= DMA_SW_RST_B | DMA_AUTO_ENABLE
						       | DMA_TIMEOUT_INT_EN | DMA_ENABLE;
			rxdma_reg->rxdma_enable_sel |= DMA_GO;
		}
	}
#endif /* ENABLE_UARXDMA */

	spin_lock_irq(&port->lock);	/* don't need to use spin_lock_irqsave() because interrupts are globally disabled */

#ifdef ENABLE_UARXDMA
	if (uarxdma)
		sp_uart_set_int_en(port->membase, SP_UART_ISC_TXM);
	else
		sp_uart_set_int_en(port->membase, SP_UART_ISC_TXM | SP_UART_ISC_RXM);
#else
	sp_uart_set_int_en(port->membase, SP_UART_ISC_TXM | SP_UART_ISC_RXM);
#endif /* ENABLE_UARXDMA */

	spin_unlock_irq(&port->lock);
	return 0;
}

/*
 * Documentation/serial/driver:
 * shutdown(port)
 * Disable the port, disable any break condition that may be in
 * effect, and free any interrupt resources.  It should not disable
 * RTS nor DTR; this will have already been done via a separate
 * call to set_mctrl.
 *
 * Drivers must not access port->info once this call has completed.
 *
 * This method will only be called when there are no more users of
 * this port.
 *
 * Locking: port_sem taken.
 * Interrupts: caller dependent.
 */
static void sunplus_uart_ops_shutdown(struct uart_port *port)
{
	unsigned long flags;
	struct sunplus_uart_port *sp_port = (struct sunplus_uart_port *)(port->private_data);

	spin_lock_irqsave(&port->lock, flags);
	sp_uart_set_int_en(port->membase, 0);	/* disable all interrupt */
	spin_unlock_irqrestore(&port->lock, flags);

	free_irq(port->irq, port);

	if (sp_port->uarxdma) {
		free_irq(sp_port->uarxdma->irq_num, port);
		DBG_INFO("free_irq(%d)\n", (int)(sp_port->uarxdma->irq_num));

		/* Buffer for UARXDMA is kept */
	}
}

/*
 * Documentation/serial/driver:
 * flush_buffer(port)
 * Flush any write buffers, reset any DMA state and stop any
 * ongoing DMA transfers.
 *
 * This will be called whenever the port->info->xmit circular
 * buffer is cleared.
 *
 * Locking: port->lock taken.
 * Interrupts: locally disabled.
 * This call must not sleep
 *
 */
#if 0
static void sunplus_uart_ops_flush_buffer(struct uart_port *port)
{
}
#endif

/*
 * Documentation/serial/driver:
 * set_termios(port,termios,oldtermios)
 * Change the port parameters, including word length, parity, stop
 * bits.  Update read_status_mask and ignore_status_mask to indicate
 * the types of events we are interested in receiving.  Relevant
 * termios->c_cflag bits are:
 *     CSIZE   - word size
 *     CSTOPB  - 2 stop bits
 *     PARENB  - parity enable
 *     PARODD  - odd parity (when PARENB is in force)
 *     CREAD   - enable reception of characters (if not set,
 *               still receive characters from the port, but
 *               throw them away.
 *     CRTSCTS - if set, enable CTS status change reporting
 *     CLOCAL  - if not set, enable modem status change
 *               reporting.
 * Relevant termios->c_iflag bits are:
 *     INPCK   - enable frame and parity error events to be
 *               passed to the TTY layer.
 *     BRKINT
 *     PARMRK  - both of these enable break events to be
 *               passed to the TTY layer.
 *
 *     IGNPAR  - ignore parity and framing errors
 *     IGNBRK  - ignore break errors,  If IGNPAR is also
 *               set, ignore overrun errors as well.
 * The interaction of the iflag bits is as follows (parity error
 * given as an example):
 * Parity error    INPCK   IGNPAR
 * n/a     0       n/a     character received, marked as
 *                         TTY_NORMAL
 * None            1       n/a character received, marked as
 *                         TTY_NORMAL
 * Yes     1       0       character received, marked as
 *                         TTY_PARITY
 * Yes     1       1       character discarded
 *
 * Other flags may be used (eg, xon/xoff characters) if your
 * hardware supports hardware "soft" flow control.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 * This call must not sleep
 */

static void sunplus_uart_ops_set_termios(struct uart_port *port, struct ktermios *termios,
					 struct ktermios *oldtermios)
{
	u32 clk, ext, div, div_l, div_h, baud;
	u32 lcr;
	unsigned long flags;

	clk = port->uartclk;

	baud = uart_get_baud_rate(port, termios, oldtermios, 0, (clk / (1 << 4)));
	/* printk("UART clock: %d, baud: %d\n", clk, baud); */
	clk += baud >> 1;
	div = clk / baud;
	ext = div & 0x0F;
	div = (div >> 4) - 1;
	div_l = (div & 0xFF) | (ext << 12);
	div_h = div >> 8;
	/* printk("div_l = %X, div_h: %X\n", div_l, div_h); */

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = SP_UART_LCR_WL5;
		break;
	case CS6:
		lcr = SP_UART_LCR_WL6;
		break;
	case CS7:
		lcr = SP_UART_LCR_WL7;
		break;
	default:	/* CS8 */
		lcr = SP_UART_LCR_WL8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= SP_UART_LCR_ST;

	if (termios->c_cflag & PARENB) {
		lcr |= SP_UART_LCR_PE;

		if (!(termios->c_cflag & PARODD))
			lcr |= SP_UART_LCR_PR;
	}
	/* printk("lcr = %X, \n", lcr); */

	/* printk("Updating UART registers...\n"); */
	spin_lock_irqsave(&port->lock, flags);

	uart_update_timeout(port, termios->c_cflag, baud);

	port->read_status_mask = 0;
	if (termios->c_iflag & INPCK)
		port->read_status_mask |= SP_UART_LSR_PE | SP_UART_LSR_FE;
	if (termios->c_iflag & (BRKINT | PARMRK))
		port->read_status_mask |= SP_UART_LSR_BC;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		port->ignore_status_mask |= SP_UART_LSR_FE | SP_UART_LSR_PE;
	if (termios->c_iflag & IGNBRK) {
		port->ignore_status_mask |= SP_UART_LSR_BC;

		if (termios->c_iflag & IGNPAR)
			port->ignore_status_mask |= SP_UART_LSR_OE;
	}

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((termios->c_cflag & CREAD) == 0) {
		port->ignore_status_mask |= SP_UART_CREAD_DISABLED;
		sp_uart_set_rx_residue(port->membase, 0);	/* flush rx data FIFO */
	}

#if 0	/* No modem pin in chip */
	if (UART_ENABLE_MS(port, termios->c_cflag))
		enable_modem_status_irqs(port);
#endif

	if (termios->c_cflag & CRTSCTS) {
		sp_uart_set_modem_ctrl(port->membase,
				       sp_uart_get_modem_ctrl(port->membase) | (SP_UART_MCR_AC | SP_UART_MCR_AR));
	} else {
		sp_uart_set_modem_ctrl(port->membase,
				       sp_uart_get_modem_ctrl(port->membase) & (~(SP_UART_MCR_AC | SP_UART_MCR_AR)));
	}

	/* do not set these in emulation */
	sp_uart_set_divider_high_register(port->membase, div_h);
	sp_uart_set_divider_low_register(port->membase, div_l);
	sp_uart_set_line_ctrl(port->membase, lcr);

	spin_unlock_irqrestore(&port->lock, flags);
}

/*
 * Documentation/serial/driver:
 * N/A.
 */
//static void sunplus_uart_ops_set_ldisc(struct uart_port *port, int new)
static void sunplus_uart_ops_set_ldisc(struct uart_port *port,
				       struct ktermios *termios)
{
	int new = termios->c_line;
	if (new == N_PPS) {
		port->flags |= UPF_HARDPPS_CD;
		sunplus_uart_ops_enable_ms(port);
	} else {
		port->flags &= ~UPF_HARDPPS_CD;
	}
}

/*
 * Documentation/serial/driver:
 * pm(port,state,oldstate)
 * Perform any power management related activities on the specified
 * port.  State indicates the new state (defined by
 * enum uart_pm_state), oldstate indicates the previous state.
 *
 * This function should not be used to grab any resources.
 *
 * This will be called when the port is initially opened and finally
 * closed, except when the port is also the system console.  This
 * will occur even if CONFIG_PM is not set.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
#if 0
static void sunplus_uart_ops_pm(struct uart_port *port, unsigned int state, unsigned int oldstate)
{
}
#endif

/*
 * Documentation/serial/driver:
 * set_wake(port,state)
 * Enable/disable power management wakeup on serial activity.  Not
 * currently implemented.
 */
#if 0
static int sunplus_uart_ops_set_wake(struct uart_port *port, unsigned int state)
{
}
#endif

/*
 * Documentation/serial/driver:
 * type(port)
 * Return a pointer to a string constant describing the specified
 * port, or return NULL, in which case the string 'unknown' is
 * substituted.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
static const char *sunplus_uart_ops_type(struct uart_port *port)
{
	struct sunplus_uart_port *sunplus_uart_port =
		(struct sunplus_uart_port *)port->private_data;
	return (sunplus_uart_port->name);
}

/*
 * Documentation/serial/driver:
 * release_port(port)
 * Release any memory and IO region resources currently in use by
 * the port.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
static void sunplus_uart_ops_release_port(struct uart_port *port)
{
	release_mem_region((resource_size_t)port->mapbase, UART_SZ);
}

/*
 * Documentation/serial/driver:
 * request_port(port)
 * Request any memory and IO region resources required by the port.
 * If any fail, no resources should be registered when this function
 * returns, and it should return -EBUSY on failure.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
static int sunplus_uart_ops_request_port(struct uart_port *port)
{
	struct sunplus_uart_port *sunplus_uart_port =
		(struct sunplus_uart_port *)port->private_data;

	/* /proc/iomem */
	if (request_mem_region((resource_size_t)port->mapbase,
			       UART_SZ,
			       sunplus_uart_port->name) == NULL) {
		return -EBUSY;
	} else {
		return 0;
	}
}

/*
 * Documentation/serial/driver:
 * config_port(port,type)
 * Perform any autoconfiguration steps required for the port.  `type`
 * contains a bit mask of the required configuration.  UART_CONFIG_TYPE
 * indicates that the port requires detection and identification.
 * port->type should be set to the type found, or PORT_UNKNOWN if
 * no port was detected.
 *
 * UART_CONFIG_IRQ indicates autoconfiguration of the interrupt signal,
 * which should be probed using standard kernel autoprobing techniques.
 * This is not necessary on platforms where ports have interrupts
 * internally hard wired (eg, system on a chip implementations).
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
static void sunplus_uart_ops_config_port(struct uart_port *port, int type)
{
	if (type & UART_CONFIG_TYPE) {
		port->type = PORT_SP;
		sunplus_uart_ops_request_port(port);
	}
}

/*
 * Documentation/serial/driver:
 * verify_port(port,serinfo)
 * Verify the new serial port information contained within serinfo is
 * suitable for this port type.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
static int sunplus_uart_ops_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	return -EINVAL;	/* Modification *serial is not allowed */
}

/*
 * Documentation/serial/driver:
 * ioctl(port,cmd,arg)
 * Perform any port specific IOCTLs.  IOCTL commands must be defined
 * using the standard numbering system found in <asm/ioctl.h>
 *
 * Locking: none.
 * Interrupts: caller dependent.
 */
#if 0
static int sunplus_uart_ops_ioctl(struct uart_port *port, unsigned int cmd, unsigned long arg)
{
}
#endif

#ifdef CONFIG_CONSOLE_POLL

/*
 * Documentation/serial/driver:
 * poll_init(port)
 * Called by kgdb to perform the minimal hardware initialization needed
 * to support poll_put_char() and poll_get_char().  Unlike ->startup()
 * this should not request interrupts.
 *
 * Locking: tty_mutex and tty_port->mutex taken.
 * Interrupts: n/a.
 */
static int sunplus_uart_ops_poll_init(struct uart_port *port)
{
	return 0;
}

/*
 * Documentation/serial/driver:
 * poll_put_char(port,ch)
 * Called by kgdb to write a single character directly to the serial
 * port.  It can and should block until there is space in the TX FIFO.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 * This call must not sleep
 */
static void sunplus_uart_ops_poll_put_char(struct uart_port *port, unsigned char data)
{
	wait_for_xmitr(port);
	sp_uart_put_char(port->membase, data);
}

/*
 * Documentation/serial/driver:
 * poll_get_char(port)
 * Called by kgdb to read a single character directly from the serial
 * port.  If data is available, it should be returned; otherwise
 * the function should return NO_POLL_CHAR immediately.
 *
 * Locking: none.
 * Interrupts: caller dependent.
 * This call must not sleep
 */
static int sunplus_uart_ops_poll_get_char(struct uart_port *port)
{
	unsigned int status;
	unsigned char data;

	do {
		status = sp_uart_get_line_status(port->membase);
	} while (!(status & SP_UART_LSR_RX));

	data = sp_uart_get_char(port->membase);
	return (int)data;
}

#endif /* CONFIG_CONSOLE_POLL */

static struct uart_ops sunplus_uart_ops = {
	.tx_empty		= sunplus_uart_ops_tx_empty,
	.set_mctrl		= sunplus_uart_ops_set_mctrl,
	.get_mctrl		= sunplus_uart_ops_get_mctrl,
	.stop_tx		= sunplus_uart_ops_stop_tx,
	.start_tx		= sunplus_uart_ops_start_tx,
	/* .send_xchar		= sunplus_uart_ops_send_xchar, */
	.stop_rx		= sunplus_uart_ops_stop_rx,
	.enable_ms		= sunplus_uart_ops_enable_ms,
	.break_ctl		= sunplus_uart_ops_break_ctl,
	.startup		= sunplus_uart_ops_startup,
	.shutdown		= sunplus_uart_ops_shutdown,
	/* .flush_buffer	= sunplus_uart_ops_flush_buffer, */
	.set_termios		= sunplus_uart_ops_set_termios,
	.set_ldisc		= sunplus_uart_ops_set_ldisc,
	/* .pm			= sunplus_uart_ops_pm, */
	/* .set_wake		= sunplus_uart_ops_set_wake, */
	.type			= sunplus_uart_ops_type,
	.release_port		= sunplus_uart_ops_release_port,
	.request_port		= sunplus_uart_ops_request_port,
	.config_port		= sunplus_uart_ops_config_port,
	.verify_port		= sunplus_uart_ops_verify_port,
	/* .ioctl				= sunplus_uart_ops_ioctl, */
#ifdef CONFIG_CONSOLE_POLL
	.poll_init		= sunplus_uart_ops_poll_init,
	.poll_put_char		= sunplus_uart_ops_poll_put_char,
	.poll_get_char		= sunplus_uart_ops_poll_get_char,
#endif /* CONFIG_CONSOLE_POLL */
};

static struct uart_driver sunplus_uart_driver;

static struct console sunplus_console = {
	.name		= DEVICE_NAME,
	.write		= sunplus_console_write,
	.device		= uart_console_device,	/* default */
	.setup		= sunplus_console_setup,
	/* .early_setup	= , */
	/*
	 * CON_ENABLED,
	 * CON_CONSDEV: preferred console,
	 * CON_BOOT: primary boot console,
	 * CON_PRINTBUFFER: used for printk buffer
	 */
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	/* .cflag	= , */
	.data		= &sunplus_uart_driver
};

static struct uart_driver sunplus_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= "Sunplus_UART",
	.dev_name	= DEVICE_NAME,
	.major		= SP_UART_MAJOR,
	.minor		= SP_UART_MINOR_START,
	.nr		= NUM_UART,
	.cons		= &sunplus_console
};

struct platform_device *sunpluse_uart_platform_device;

static void sunplus_uart_init_config(void)
{
	/* Enable all uart ports */
	uart_enable_status = ~0;
	/* DBG_INFO("uart_enable_status: 0x%08X\n", uart_enable_status); */

	/*
	 * Bind DMA-RX by setting
	 * sp_uart[x].uarxdma = (struct uarxdma_info *)(&uarxdma[n]), where n = 0, 1.
	 */
#if (UARXDMA0 != -1)
	sp_uart[UARXDMA0].uarxdma = (struct uarxdma_info *)(&uarxdma[0]);
	sunplus_uart_ports[UARXDMA0].uarxdma = (struct uarxdma_info *)(&uarxdma[0]);
#endif
#if (UARXDMA1 != -1)
	sp_uart[UARXDMA1].uarxdma = (struct uarxdma_info *)(&uarxdma[1]);
	sunplus_uart_ports[UARXDMA1].uarxdma = (struct uarxdma_info *)(&uarxdma[1]);
#endif
}

//TODO: move clk info to dts
#if 0
u32 sp_uart_get_clk(void)
{
	u32 clk;
#if defined(CONFIG_MACH_PENTAGRAM_8388_ACHIP) || defined(CONFIG_MACH_PENTAGRAM_8388_BCHIP)
	clk = 270 * 1000 * 1000; // sysslow 270M
#else
	clk = 27 * 1000 * 1000; // extclk 27M
#endif
	return clk;
}
#endif

static int sunplus_uart_platform_driver_probe_of(struct platform_device *pdev)
{
	struct resource *res_mem, *res_irq;
	struct uart_port *port;
	struct clk *clk;
	int ret;

	if (pdev->dev.of_node) {
		pdev->id = of_alias_get_id(pdev->dev.of_node, "serial");
		if (pdev->id < 0)
			pdev->id = of_alias_get_id(pdev->dev.of_node, "uart");
	}

	if (pdev->id < 0 || pdev->id >= NUM_UART)
		return -EINVAL;

	port = &sunplus_uart_ports[pdev->id].uport;
	if (port->membase) {
		return -EBUSY;
	}
	memset(port, 0, sizeof(*port));

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	port->mapbase = res_mem->start;
	port->membase = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(port->membase))
		return PTR_ERR(port->membase);

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq)
		return -ENODEV;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		port->uartclk = (27 * 1000 * 1000); /* default */
	} else {
		port->uartclk = clk_get_rate(clk);
	}

	port->iotype = UPIO_MEM;
	port->irq = res_irq->start;
	port->ops = &sunplus_uart_ops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->dev = &pdev->dev;
	port->fifosize = 16;
	port->line = pdev->id;

	if (pdev->id == 0)
		port->cons = &sunplus_console;

	port->private_data = container_of(&sunplus_uart_ports[pdev->id].uport, struct sunplus_uart_port, uport);
	sprintf(sunplus_uart_ports[pdev->id].name, "sp_uart%d", pdev->id);

	ret = uart_add_one_port(&sunplus_uart_driver, port);
	if (ret) {
		port->membase = NULL;
		return ret;
	}
	platform_set_drvdata(pdev, port);
	return 0;
}

static const struct of_device_id sp_uart_of_match[] = {
	{ .compatible = "sunplus,sp-uart" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_serial_of_match);

static struct platform_driver sunplus_uart_platform_driver = {
	.probe		= sunplus_uart_platform_driver_probe_of,
	.driver = {
		.name	= DEVICE_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sp_uart_of_match),
	}
};

static int __init sunplus_uart_init(void)
{
	int ret;

	/* DBG_INFO("uart0_as_console: %X\n", uart0_as_console); */

	sunplus_uart_init_config();

	if (!uart0_as_console || !(uart_enable_status & 0x01))
		sunplus_uart_driver.cons = NULL;

	/* /proc/tty/driver/(sunplus_uart_driver->driver_name) */
	ret = uart_register_driver(&sunplus_uart_driver);
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&sunplus_uart_platform_driver);
	if (ret != 0) {
		uart_unregister_driver(&sunplus_uart_driver);
		return ret;
	}

	return 0;
}
__initcall(sunplus_uart_init);

module_param(uart0_as_console, uint, S_IRUGO);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sunplus Technology");
MODULE_DESCRIPTION("Sunplus UART driver");
