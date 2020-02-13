#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include <asm/firmware.h>



#ifdef CONFIG_HAVE_ARM_SCU
#include <asm/smp_scu.h>
#endif

#define OF_CPU_BOOT       "cpu-boot-reg"

extern void sc_smp_cpu_die(unsigned int cpu);

#ifdef CONFIG_HOTPLUG_CPU

static bool sc_smp_cpu_can_disable(unsigned int cpu)
{
	/* We assume all CPUs may be shut down. */

	//pr_info("sc_smp_cpu_can_disable\n");

	return true;
}


#endif /* CONFIG_HOTPLUG_CPU */


static u32 secondary_boot_addr_for(unsigned int cpu)
{
        u32 secondary_boot_addr = 0;
        struct device_node *cpu_node = of_get_cpu_node(cpu, NULL);

        if (!cpu_node) {
                pr_err("Failed to find device tree node for CPU%u\n", cpu);
                return 0;
        }

        if (of_property_read_u32(cpu_node, OF_CPU_BOOT, &secondary_boot_addr))
                pr_err("missed cpu boot in DT for %u\n", cpu);

        of_node_put(cpu_node);

        return secondary_boot_addr;
}

#ifdef CONFIG_HAVE_ARM_SCU
static const struct of_device_id sc_scu_match[] __initconst = {
        { .compatible = "arm,cortex-a9-scu", },
        {}
};
#endif

static void __init sc_smp_prepare_cpus(unsigned int max_cpus)
{
#ifdef CONFIG_HAVE_ARM_SCU
	struct device_node *scu = of_find_matching_node(NULL, sc_scu_match);
	if (scu) {
		scu_enable(of_iomap(scu, 0));
	}
#endif
}

static int sc_smp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
        const u32 wait_addr = secondary_boot_addr_for(cpu);
		u32 mpidr = cpu_logical_map(cpu);
		u32 core_id = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	    void __iomem *va;

	pr_info("sc_smp_boot_secondary cpu%d, cpu_id %d\n", cpu, core_id);


        if (!wait_addr)
                return -EINVAL;

        va = ioremap_nocache((phys_addr_t)wait_addr, sizeof(phys_addr_t));
        if (!va) {
                pr_warn("unable to ioremap cpu start pos\n");
                return -ENOMEM;
        }


	writel(__pa_symbol(secondary_startup), va);


	/* Ensure the write is visible to the secondary core */
     smp_wmb();

     iounmap(va);

	 sev();

	return 0;

}

static const struct smp_operations sc_smp_ops __initconst = {
	.smp_prepare_cpus	= sc_smp_prepare_cpus,
	.smp_boot_secondary	= sc_smp_boot_secondary,
	#ifdef CONFIG_HOTPLUG_CPU
	.cpu_can_disable	= sc_smp_cpu_can_disable,	
	.cpu_die		    = sc_smp_cpu_die,
     #endif
};
CPU_METHOD_OF_DECLARE(sc_smp, "sunplus,sc-smp", &sc_smp_ops);
