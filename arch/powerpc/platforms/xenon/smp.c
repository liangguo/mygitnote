/*
 * SMP support for Xenon machines.
 *
 * Based on CBE's smp.c.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <asm/machdep.h>
#include "interrupt.h"

static int __init smp_xenon_probe(void)
{
	xenon_request_IPIs();

	return cpus_weight(CPU_MASK_ALL);
}

static void __devinit smp_xenon_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xenon_init_irq_on_cpu(cpu);
}

static int __devinit smp_xenon_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	pr_debug("smp_xenon_kick_cpu %d\n", nr);

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;

	return 0;
}

static int smp_xenon_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.  Odd-numbered
	 * cpus are assumed to be secondary threads.
	 */
	if (system_state < SYSTEM_RUNNING &&
	    cpu_has_feature(CPU_FTR_SMT) &&
	    !smt_enabled_at_boot && nr % 2 != 0)
		return 0;

	return 1;
}

extern void xenon_cause_IPI(int target, int msg);

static void smp_xenon_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		xenon_cause_IPI(target, msg);
	} else {
		for_each_online_cpu(i) {
			/*
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			*/
			xenon_cause_IPI(i, msg);
		}
	}
}

static struct smp_ops_t xenon_smp_ops = {
	.message_pass	= smp_xenon_message_pass,
	.probe		= smp_xenon_probe,
	.kick_cpu	= smp_xenon_kick_cpu,
	.setup_cpu	= smp_xenon_setup_cpu,
	.cpu_bootable	= smp_xenon_cpu_bootable,
};

/* This is called very early */
void __init smp_init_xenon(void)
{
	pr_debug(" -> smp_init_xenon()\n");

	smp_ops = &xenon_smp_ops;

	pr_debug(" <- smp_init_xenon()\n");
}
