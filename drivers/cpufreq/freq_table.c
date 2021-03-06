/*
 * linux/drivers/cpufreq/freq_table.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/nuisetting.h>

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/
int nui_old_freq = 1134000;
int nui_old_freq_two = 1350000;
int nui_old_freq_three = 1620000;


int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table)
{
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int i;

	if (table[NUI_BATT_SAV_FREQ_LEV].frequency != CPUFREQ_TABLE_END)
		nui_old_freq = table[NUI_BATT_SAV_FREQ_LEV].frequency;
	if (table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency != CPUFREQ_TABLE_END)
		nui_old_freq_two = table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency;
	if (table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency != CPUFREQ_TABLE_END)
		nui_old_freq_three = table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID) {
			//pr_debug("ngxson: table entry %u is invalid, skipping\n", i);

			continue;
		}
		//pr_debug("ngxson: table entry %u: %u kHz, %u index\n",
		//			i, freq, table[i].index);
		if (freq < min_freq)
			min_freq = freq;
		if (freq > max_freq)
			max_freq = freq;
	}

	policy->min = policy->cpuinfo.min_freq = min_freq;
	policy->max = policy->cpuinfo.max_freq = max_freq;

	if (policy->min == ~0)
		return -EINVAL;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_cpuinfo);


int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table)
{
	unsigned int next_larger = ~0;
	unsigned int i;
	unsigned int count = 0;

	//pr_debug("ngxson: request for verification of policy (%u - %u kHz) for cpu %u\n",
	//				policy->min, policy->max, policy->cpu);

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq >= policy->min) && (freq <= policy->max))
			count++;
		else if ((next_larger > freq) && (freq > policy->max))
			next_larger = freq;
	}

	if (!count)
		policy->max = next_larger;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	//pr_debug("ngxson: verification lead to (%u - %u kHz) for cpu %u\n",
	//			policy->min, policy->max, policy->cpu);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_verify);


int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index)
{
	struct cpufreq_frequency_table optimal = {
		.index = ~0,
		.frequency = 0,
	};
	struct cpufreq_frequency_table suboptimal = {
		.index = ~0,
		.frequency = 0,
	};
	unsigned int i;

	//pr_debug("ngxson: request for target %u kHz (relation: %u) for cpu %u\n",
	//				target_freq, relation, policy->cpu);

	switch (relation) {
	case CPUFREQ_RELATION_H:
		suboptimal.frequency = ~0;
		break;
	case CPUFREQ_RELATION_L:
		optimal.frequency = ~0;
		break;
	}

	if (!cpu_online(policy->cpu))
		return -EINVAL;

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		unsigned int freq = table[i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		if ((freq < policy->min) || (freq > policy->max))
			continue;
		switch (relation) {
		case CPUFREQ_RELATION_H:
			if (freq <= target_freq) {
				if (freq >= optimal.frequency) {
					optimal.frequency = freq;
					optimal.index = i;
				}
			} else {
				if (freq <= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.index = i;
				}
			}
			break;
		case CPUFREQ_RELATION_L:
			if (freq >= target_freq) {
				if (freq <= optimal.frequency) {
					optimal.frequency = freq;
					optimal.index = i;
				}
			} else {
				if (freq >= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.index = i;
				}
			}
			break;
		}
	}
	if (optimal.index > i) {
		if (suboptimal.index > i)
			return -EINVAL;
		*index = suboptimal.index;
	} else
		*index = optimal.index;

	//pr_debug("ngxson: target is %u (%u kHz, %u)\n", *index, table[*index].frequency,
	//	table[*index].index);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_target);

static DEFINE_PER_CPU(struct cpufreq_frequency_table *, cpufreq_show_table);
/**
 * show_available_freqs - show available frequencies for the specified CPU
 */
static ssize_t show_available_freqs(struct cpufreq_policy *policy, char *buf)
{
	unsigned int i = 0;
	unsigned int cpu = policy->cpu;
	ssize_t count = 0;
	struct cpufreq_frequency_table *table;

	if (!per_cpu(cpufreq_show_table, cpu))
		return -ENODEV;

	table = per_cpu(cpufreq_show_table, cpu);
	//printk("ngxson: debug freq_table.c TABLE_END=%d\n", CPUFREQ_TABLE_END);
	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID) {
			//printk("ngxson: debug freq_table.c ENTRY_INVALID=%d\n", CPUFREQ_ENTRY_INVALID);
			continue;
		}
		//printk("ngxson: index=%d freq=%d\n", i, table[i].frequency);
		count += sprintf(&buf[count], "%d ", table[i].frequency);
	}
	count += sprintf(&buf[count], "\n");

	return count;

}

struct freq_attr cpufreq_freq_attr_scaling_available_freqs = {
	.attr = { .name = "scaling_available_frequencies",
		  .mode = 0444,
		},
	.show = show_available_freqs,
};
EXPORT_SYMBOL_GPL(cpufreq_freq_attr_scaling_available_freqs);

//NUI Battery Saving mode
void nui_tune_cpu(int cpu, int m) {
	struct cpufreq_frequency_table *table;

	table = per_cpu(cpufreq_show_table, cpu);

	if (table[NUI_BATT_SAV_FREQ_LEV].frequency != CPUFREQ_TABLE_END)
		nui_old_freq = table[NUI_BATT_SAV_FREQ_LEV].frequency;
	if (table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency != CPUFREQ_TABLE_END)
		nui_old_freq_two = table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency;
	if (table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency != CPUFREQ_TABLE_END)
		nui_old_freq_three = table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency;

	if(m == 1) {
		table[NUI_BATT_SAV_FREQ_LEV].frequency = CPUFREQ_TABLE_END;
	} else if(m == 2) {
		table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency = CPUFREQ_TABLE_END;
	} else if(m == 3) {
		table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency = CPUFREQ_TABLE_END;
	} else {
		table[NUI_BATT_SAV_FREQ_LEV].frequency = nui_old_freq;
		table[NUI_BATT_SAV_FREQ_LEV_TWO].frequency = nui_old_freq_two;
		table[NUI_BATT_SAV_FREQ_LEV_THREE].frequency = nui_old_freq_three;
	}
}

void nui_batt_sav_mode(int m)
{
	nui_tune_cpu(0, 0);
	nui_tune_cpu(1, 0);
	nui_tune_cpu(0, m);
	nui_tune_cpu(1, m);
}

/*
 * if you use these, you must assure that the frequency table is valid
 * all the time between get_attr and put_attr!
 */
void cpufreq_frequency_table_get_attr(struct cpufreq_frequency_table *table,
				      unsigned int cpu)
{
	//pr_debug("setting show_table for cpu %u to %p\n", cpu, table);
	per_cpu(cpufreq_show_table, cpu) = table;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_get_attr);

void cpufreq_frequency_table_put_attr(unsigned int cpu)
{
	//pr_debug("clearing show_table for cpu %u\n", cpu);
	per_cpu(cpufreq_show_table, cpu) = NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_table_put_attr);

struct cpufreq_frequency_table *cpufreq_frequency_get_table(unsigned int cpu)
{
	return per_cpu(cpufreq_show_table, cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_frequency_get_table);

MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("CPUfreq frequency table helpers");
MODULE_LICENSE("GPL");
