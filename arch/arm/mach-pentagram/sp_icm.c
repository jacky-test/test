#include <linux/module.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <mach/sp_icm.h>

#define NUM_ICM 4

#define TRACE	printk("[SP_ICM]%s:%d\n", __FUNCTION__, __LINE__)

// cfg0
#define ICM_ENABLE		0x00010001	// enable icm
#define ICM_DISABLE		0x00010000	// disable icm
#define ICM_RELOAD		0x00020002	// reload icm settings
#define ICM_INTCLR		0x00040004	// clear icm interrupt

#define MUXSEL_OFS	3
#define MUXSEL_BITS	3
#define CLKSEL_OFS	6
#define CLKSEL_BITS	3

// cfg1
#define EEMODE_OFS	0
#define EEMODE_BITS	2
#define ETIMES_OFS	2
#define ETIMES_BITS	4
#define DTIMES_OFS	6
#define DTIMES_BITS	3

#define ICM_FCLEAR	0x20002000	// clear fifo, also fddrop=0, fempty=1, ffull=0
#define ICM_FMASK	(ICM_FDDROP | ICM_FEMPTY | ICM_FFULL)

#define ICM_MSK(field)  (((1 << field##_BITS) - 1) << field##_OFS)

#define ICM_SETCFG(_cfg, field, val) \
	do { \
		icm->cfg##_cfg = ((val) << field##_OFS) | (ICM_MSK(field) << 16); \
	} while (0)
#define ICM_GETCFG(_cfg, field) \
	(((icm->cfg##_cfg) & ICM_MSK(field)) >> field##_OFS)

struct sp_icm_reg {
	/*
	u32 enable:1;
	u32 reload:1; // reload setting
	u32 intclr:1; // write 1 clear interrupt
	u32 muxsel:3; // select input signal source
	u32 clksel:3; // select clock source for counter
	u32 rsv0:7;
	u32 msk0:16;
	*/
	u32 cfg0;

	/*
	u32 eemode:2; // edge mode: 0 rising / 1 falling / 2 both
	u32 etimes:4; // event times (0~15)
	u32 dtimes:3; // debounce times (0~7)
	u32 rsv1:3;
	u32 fddrop:1; // fifo data drop
	u32 fclear:1; // fifo clear
	u32 fempty:1; // fifo empty
	u32 ffull :1; // fifo full
	u32 msk1:16;
	*/
	u32 cfg1;

	u32 cntscl;	// counter clock prescaler: cnt_clk = ext_clk / (cnt_scl + 1)
	u32 tstscl;	// test signal clock prescaler: tst_clk = sysclk / (tst_scl + 1)
	u32 cnt;	// counter, read from fifo

	u32 pwh;	// pulse width high
	u32 pwl;	// pulse width low
};

struct sp_icm_dev {
	volatile struct sp_icm_reg *reg;
	int irq;
};

static struct sp_icm_dev sp_icm;
static sp_icm_cbf cbfs[NUM_ICM];

static irqreturn_t sp_icm_isr(int irq, void *dev_id)
{
	int i = irq - sp_icm.irq;
	volatile struct sp_icm_reg *icm = &sp_icm.reg[i];
	u32 cnt, fstate;

	//TRACE;
	icm->cfg0 = ICM_INTCLR; // clear interrupt

	while (!((fstate = icm->cfg1) & ICM_FEMPTY)) { // fifo not empty
		cnt = icm->cnt; // read counter from fifo
		if (cbfs[i]) {
			(*cbfs[i])(i, cnt, fstate & ICM_FMASK); // callback
		}
	}

	return IRQ_HANDLED;
}



int sp_icm_setcfg(int i, struct sp_icm_cfg *cfg)
{
	volatile struct sp_icm_reg *icm;

	if (i < 0 || i >= NUM_ICM) return -EINVAL;

	icm = &sp_icm.reg[i];
	ICM_SETCFG(0, MUXSEL, cfg->muxsel);
	ICM_SETCFG(0, CLKSEL, cfg->clksel);
	ICM_SETCFG(1, EEMODE, cfg->eemode);
	ICM_SETCFG(1, ETIMES, cfg->etimes);
	ICM_SETCFG(1, DTIMES, cfg->dtimes);
	icm->cntscl = cfg->cntscl;
	icm->tstscl = cfg->tstscl;

	return 0;
}
EXPORT_SYMBOL(sp_icm_setcfg);

int sp_icm_getcfg(int i, struct sp_icm_cfg *cfg)
{
	volatile struct sp_icm_reg *icm;

	if (i < 0 || i >= NUM_ICM) return -EINVAL;

	icm = &sp_icm.reg[i];
	cfg->muxsel = ICM_GETCFG(0, MUXSEL);
	cfg->clksel = ICM_GETCFG(0, CLKSEL);
	cfg->eemode = ICM_GETCFG(1, EEMODE);
	cfg->etimes = ICM_GETCFG(1, ETIMES);
	cfg->dtimes = ICM_GETCFG(1, DTIMES);
	cfg->cntscl = icm->cntscl;
	cfg->tstscl = icm->tstscl;

	return 0;
}
EXPORT_SYMBOL(sp_icm_getcfg);

int sp_icm_reload(int i)
{
	if (i < 0 || i >= NUM_ICM) return -EINVAL;
	sp_icm.reg[i].cfg0 = ICM_RELOAD;
	return 0;
}
EXPORT_SYMBOL(sp_icm_reload);

int sp_icm_enable(int i, sp_icm_cbf cbf)
{
	if (i < 0 || i >= NUM_ICM) return -EINVAL;
	cbfs[i] = cbf;
	sp_icm.reg[i].cfg0 = ICM_ENABLE;
	return 0;
}
EXPORT_SYMBOL(sp_icm_enable);

int sp_icm_disable(int i)
{
	if (i < 0 || i >= NUM_ICM) return -EINVAL;
	sp_icm.reg[i].cfg0 = ICM_DISABLE;
	sp_icm.reg[i].cfg1 = ICM_FCLEAR; // clear fifo
	cbfs[i] = NULL;
	return 0;
}
EXPORT_SYMBOL(sp_icm_disable);

int sp_icm_fstate(int i, u32 *fstate)
{
	if (i < 0 || i >= NUM_ICM) return -EINVAL;
	*fstate = sp_icm.reg[i].cfg1 & ICM_FMASK;
	return 0;
}
EXPORT_SYMBOL(sp_icm_fstate);

int sp_icm_pwidth(int i, u32 *pwh, u32 *pwl)
{
	if (i < 0 || i >= NUM_ICM) return -EINVAL;
	*pwh = sp_icm.reg[i].pwh;
	*pwl = sp_icm.reg[i].pwl;
	return 0;
}
EXPORT_SYMBOL(sp_icm_pwidth);


//#ifdef CONFIG_ICM_TEST // test & example
#if 1
static u32 tscnt = 0; // test signal counter

static void test_cbf(int i, u32 cnt, u32 fstate)
{
	u32 pwh, pwl;
	sp_icm_pwidth(i, &pwh, &pwl);
	printk("icm%d_%05u: %10u %04x %u %u\n", i, ++tscnt, cnt, fstate, pwh, pwl);
}

static void test_help(void)
{
	printk(
		"sp_icm test:\n"
		"  echo <icm:0~3> [function] [params...] > /sys/module/sp_icm/parameters/test\n"
		"  * if no function & params, dump icm cfg & state\n"
		"  function:\n"
		"    0: disable icm, no params\n"
		"    1: enable icm\n"
		"    2: reload icm\n"
		"  params in order:\n"
		"    muxsel, clksel, eemode, etimes, dtimes, cntscl, tstscl, tstime(ms)\n"
		"    * -1 means no change\n"
	);
}

static int test_set(const char *val, const struct kernel_param *kp)
{
	int i, f, muxsel, clksel, eemode, etimes, dtimes, cntscl, tstscl, tstime;
	u32 fstate, pwh, pwl;
	struct sp_icm_cfg cfg;

	i = f = muxsel = clksel = eemode = etimes = dtimes = cntscl = tstscl = tstime = -1;
	sscanf(val, "%d %d %d %d %d %d %d %d %d %d",
		&i, &f, &muxsel, &clksel, &eemode, &etimes, &dtimes, &cntscl, &tstscl, &tstime);

	if (i >= 0 && i < NUM_ICM) {
		switch (f) {
		case 0: // disable
disable:
			sp_icm_disable(i);
			printk("icm%d: tscnt = %u\n", i, tscnt);
			tscnt = 0;
			return 0;

		case 1: // enable
		case 2: // reload
			sp_icm_getcfg(i, &cfg);
			if (muxsel != -1) cfg.muxsel = (u32)muxsel;
			if (clksel != -1) cfg.clksel = (u32)clksel;
			if (eemode != -1) cfg.eemode = (u32)eemode;
			if (etimes != -1) cfg.etimes = (u32)etimes;
			if (dtimes != -1) cfg.dtimes = (u32)dtimes;
			if (cntscl != -1) cfg.cntscl = (u32)cntscl;
			if (tstscl != -1) cfg.tstscl = (u32)tstscl;
			sp_icm_setcfg(i, &cfg);
			if (f == 1) {
				sp_icm_enable(i, test_cbf);
				if (tstime > 0) {
					mdelay(tstime);
					goto disable;
				}
			} else
				sp_icm_reload(i);
			return 0;

		case -1:
			sp_icm_getcfg(i, &cfg);
			sp_icm_fstate(i, &fstate);
			sp_icm_pwidth(i, &pwh, &pwl);
			printk("sp_icm%d cfg & state:", i);
			printk("muxsel: %u", cfg.muxsel);
			printk("clksel: %u", cfg.clksel);
			printk("eemode: %u", cfg.eemode);
			printk("etimes: %u", cfg.etimes);
			printk("dtimes: %u", cfg.dtimes);
			printk("cntscl: %u", cfg.cntscl);
			printk("tstscl: %u", cfg.tstscl);
			printk("fstate: %04x", fstate);
			printk("pwidth: %u %u\n", pwh, pwl);
			return 0;
		}
	}

	test_help();
	return 0;
}

static const struct kernel_param_ops test_ops = {
	.set = test_set,
};
module_param_cb(test, &test_ops, NULL, 0600);
#endif


static int sp_icm_probe(struct platform_device *pdev)
{
	struct sp_icm_dev *dev = &sp_icm;
	struct resource *res_mem, *res_irq;
	void __iomem *membase;
	int i = 0;
	int ret = 0;

	TRACE;
	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	membase = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(membase))
		return PTR_ERR(membase);

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq) {
		ret = -ENODEV;
		goto out;
	}

	dev->reg = membase;
	dev->irq = res_irq->start;
	platform_set_drvdata(pdev, dev);

	while (i < NUM_ICM) {
		ret = request_irq(dev->irq + i, sp_icm_isr, IRQF_TRIGGER_RISING, "sp_icm", dev);
		if (ret) goto out;
		i++;
	}
	TRACE;

out:
	if (ret) {
		TRACE;
		while (i--) free_irq(dev->irq + i, dev);
		devm_iounmap(&pdev->dev, membase);
	}

	return ret;
}

static int sp_icm_remove(struct platform_device *pdev)
{
	struct sp_icm_dev *dev = platform_get_drvdata(pdev);
	int i = NUM_ICM;

	while (i--) free_irq(dev->irq + i, dev);
	devm_iounmap(&pdev->dev, (void *)dev->reg);

	return 0;
}

static const struct of_device_id sp_icm_of_match[] = {
	{ .compatible = "sunplus,sp-icm" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sp_crypto_of_match);

static struct platform_driver sp_icm_driver = {
	.probe		= sp_icm_probe,
	.remove		= sp_icm_remove,
	.driver		= {
		.name	= "sp_icm",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(sp_icm_of_match),
	},
};

static int __init sp_icm_module_init(void)
{
	platform_driver_register(&sp_icm_driver);
	return 0;
}

static void __exit sp_icm_module_exit(void)
{
	platform_driver_unregister(&sp_icm_driver);
}

module_init(sp_icm_module_init);
module_exit(sp_icm_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sunplus Technology");
MODULE_DESCRIPTION("Sunplus ICM driver");
