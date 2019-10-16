#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/usb/phy.h>
#include <linux/usb/sp_usb.h>
#include <linux/nvmem-consumer.h>


static struct clk *uphy0_clk;
static struct resource *uphy0_res_mem;

#if 0
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
#endif

char *otp_read_disc0(struct device *_d, ssize_t *_l, char *_name)
{
	char *ret = NULL;
	struct nvmem_cell *c = nvmem_cell_get(_d, _name);

	if (IS_ERR_OR_NULL(c)) {
		dev_err(_d, "OTP %s read failure: %ld", _name, PTR_ERR(c));
		return (NULL);
	}

	ret = nvmem_cell_read(c, _l);
	nvmem_cell_put(c);
	dev_dbg(_d, "%d bytes read from OTP %s", *_l, _name);

	return (ret);
}

static void uphy0_init(struct platform_device *pdev)
{
	u32 val, set;
	void __iomem *usb_otp_reg;
	void __iomem *regs = (void __iomem *)B_SYSTEM_BASE;
	char *disc_name = "disc_vol0";
	ssize_t otp_l = 0;
	char *otp_v;

	usb_otp_reg = ioremap_nocache(USB_OTP_REG, 1);

	/* 1. Default value modification */
	writel(RF_MASK_V(0xffff, 0x4002), regs + UPHY0_CTL0_OFFSET);
	writel(RF_MASK_V(0xffff, 0x8747), regs + UPHY0_CTL1_OFFSET);

	/* 2. PLL power off/on twice */
	writel(RF_MASK_V(0xffff, 0x88), regs + UPHY0_CTL3_OFFSET);
	mdelay(1);
	writel(RF_MASK_V(0xffff, 0x80), regs + UPHY0_CTL3_OFFSET);
	mdelay(1);
	writel(RF_MASK_V(0xffff, 0x88), regs + UPHY0_CTL3_OFFSET);
	mdelay(1);
	writel(RF_MASK_V(0xffff, 0x80), regs + UPHY0_CTL3_OFFSET);
	mdelay(1);
	writel(RF_MASK_V(0xffff, 0x00), regs + UPHY0_CTL3_OFFSET);

	/* 3. reset UPHY0 */
	writel(RF_MASK_V_SET(1 << 13), regs + USB_RESET_OFFSET);
	writel(RF_MASK_V_CLR(1 << 13), regs + USB_RESET_OFFSET);
	mdelay(1);

	/* 4.b board uphy 0 internal register modification for tid certification */
	otp_v = otp_read_disc0(&pdev->dev, &otp_l, disc_name);
	set = *otp_v & OTP_DISC_LEVEL_BIT;
	if (set == 0)
		set = 0xD;

	val = readl(uphy0_base_addr + DISC_LEVEL_OFFSET);
	val = (val & ~OTP_DISC_LEVEL_BIT) | set;
	writel(val, uphy0_base_addr + DISC_LEVEL_OFFSET);

	val = readl(uphy0_base_addr + ECO_PATH_OFFSET);
	val &= ~(ECO_PATH_SET);
	writel(val, uphy0_base_addr + ECO_PATH_OFFSET);

	val = readl(uphy0_base_addr + POWER_SAVING_OFFSET);
	val &= ~(POWER_SAVING_SET);
	writel(val, uphy0_base_addr + POWER_SAVING_OFFSET);

	/* 5. USBC 0 reset */
	writel(RF_MASK_V_SET(1 << 10), regs + USB_RESET_OFFSET);
	writel(RF_MASK_V_CLR(1 << 10), regs + USB_RESET_OFFSET);

	/* port 0 uphy clk fix */
	writel(RF_MASK_V_SET(1 << 6), regs + UPHY0_CTL2_OFFSET);

	/* 6. switch to host */
	writel(RF_MASK_V_SET(3 << 4), regs + USBC_CTL_OFFSET);

#ifdef CONFIG_USB_SUNPLUS_OTG
	writel(RF_MASK_V_SET(1 << 2), regs + PIN_MUX_CTRL);
	writel(RF_MASK_V_CLR(1 << 4), regs + USBC_CTL_OFFSET);
	mdelay(1);
#endif

	/* 7. AC & ACB */
	writel(RF_MASK_V_SET(1 << 11), regs + UPHY0_CTL3_OFFSET);
	writel(RF_MASK_V_SET(1 << 14), regs + UPHY0_CTL3_OFFSET);

	iounmap(usb_otp_reg);
}

static int sunplus_usb_phy0_probe(struct platform_device *pdev)
{
#if 0
	s32 ret;
	u32 port_id = 0;

	usb_vbus_gpio[port_id] = of_get_named_gpio(pdev->dev.of_node, "vbus-gpio", 0);
	if ( !gpio_is_valid( usb_vbus_gpio[port_id]))
		printk(KERN_NOTICE "Wrong pin %d configured for vbus", usb_vbus_gpio[port_id]);
	ret = gpio_request( usb_vbus_gpio[port_id], "usb0-vbus");
	if ( ret < 0)
		printk(KERN_NOTICE "Can't get vbus pin %d", usb_vbus_gpio[port_id]);
	printk(KERN_NOTICE "%s,usb0_vbus_gpio_pin:%d\n",__FUNCTION__,usb_vbus_gpio[port_id]);
#endif

	/*enable uphy0 system clock*/
	uphy0_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(uphy0_clk)) {
		pr_err("not found clk source\n");
		return PTR_ERR(uphy0_clk);
	}
	clk_prepare(uphy0_clk);
	clk_enable(uphy0_clk);

	uphy0_irq_num = platform_get_irq(pdev, 0);
	if (uphy0_irq_num < 0) {
		printk(KERN_NOTICE "no irq provieded,ret:%d\n",uphy0_irq_num);
		return uphy0_irq_num;
	}
	printk(KERN_NOTICE "uphy0_irq:%d\n",uphy0_irq_num);

	uphy0_res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!uphy0_res_mem) {
		printk(KERN_NOTICE "no memory recourse provieded");
		return -ENXIO;
	}

	if (!request_mem_region(uphy0_res_mem->start, resource_size(uphy0_res_mem), "uphy0")) {
		printk(KERN_NOTICE "hw already in use");
		return -EBUSY;
	}

	uphy0_base_addr = ioremap_nocache(uphy0_res_mem->start, resource_size(uphy0_res_mem));
	if (!uphy0_base_addr){
		release_mem_region(uphy0_res_mem->start, resource_size(uphy0_res_mem));
		 return -EFAULT;
	}

	uphy0_init(pdev);

	writel(0x19, uphy0_base_addr + CDP_REG_OFFSET);
	writel(0x92, uphy0_base_addr + DCP_REG_OFFSET);
	writel(0x17, uphy0_base_addr + UPHY_INTER_SIGNAL_REG_OFFSET);

	return 0;
}

static int sunplus_usb_phy0_remove(struct platform_device *pdev)
{
	u32 val;
#if 0
	u32 port_id = 0;
#endif
	void __iomem *regs = (void __iomem *)B_SYSTEM_BASE;

	val = readl(uphy0_base_addr + CDP_REG_OFFSET);
	val &= ~(1u << CDP_OFFSET);
	writel(val, uphy0_base_addr + CDP_REG_OFFSET);

	iounmap(uphy0_base_addr);
	release_mem_region(uphy0_res_mem->start, resource_size(uphy0_res_mem));

	/* pll power off */
	writel(RF_MASK_V(0xffff, 0x88), regs + UPHY0_CTL3_OFFSET);
	/*disable uphy0 system clock*/
	clk_disable(uphy0_clk);
#if 0
	gpio_free(usb_vbus_gpio[port_id]);
#endif

	return 0;
}

static const struct of_device_id phy0_sunplus_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-usb-phy0" },
	{ }
};

MODULE_DEVICE_TABLE(of, phy0_sunplus_dt_ids);

static struct platform_driver sunplus_usb_phy0_driver = {
	.probe		= sunplus_usb_phy0_probe,
	.remove		= sunplus_usb_phy0_remove,
	.driver		= {
		.name	= "sunplus-usb-phy0",
		.of_match_table = phy0_sunplus_dt_ids,
	},
};


static int __init usb_phy0_sunplus_init(void)
{
	if (sp_port_enabled & PORT0_ENABLED) {
		printk(KERN_NOTICE "register sunplus_usb_phy0_driver\n");
		return platform_driver_register(&sunplus_usb_phy0_driver);	
	} else {
		printk(KERN_NOTICE "uphy0 not enabled\n");
		return 0;
	}
}
fs_initcall(usb_phy0_sunplus_init);

static void __exit usb_phy0_sunplus_exit(void)
{
	if (sp_port_enabled & PORT0_ENABLED) {
		printk(KERN_NOTICE "unregister sunplus_usb_phy0_driver\n");
		platform_driver_unregister(&sunplus_usb_phy0_driver);
	} else {
		printk(KERN_NOTICE "uphy0 not enabled\n");
		return;
	}
}
module_exit(usb_phy0_sunplus_exit);


MODULE_ALIAS("sunplus_usb_phy0");
MODULE_LICENSE("GPL");
