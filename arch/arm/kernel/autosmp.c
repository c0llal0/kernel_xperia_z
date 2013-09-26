/*
 * arch/arm/kernel/autosmp.c
 *
 * automatically hotplug/unplug multiple cpu cores
 * based on cpu load and suspend state
 *
 * based on the msm_mpdecision code by
 * Copyright (c) 2012-2013, Dennis Rassmann <showp1984@gmail.com>
 *
 * Copyright (C) 2013-2014, Rauf Gungor, http://github.com/mrg666
 * rewrite to simplify and optimize, Jul. 2013, http://goo.gl/cdGw6x
 * optimize more, generalize for n cores, Sep. 2013, http://goo.gl/448qBz
 * generalize for all arch, rename as autosmp, Dec. 2013, http://goo.gl/x5oyhy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. For more details, see the GNU
 * General Public License included with the Linux kernel or available
 * at www.gnu.org/licenses
 */

#define pr_fmt(fmt) "autosmp: " fmt

#include <linux/moduleparam.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>

#define ASMP_STARTDELAY		20000
#define ASMP_WORK_DELAY		100
#define ASMP_MIN_CPUS		1
#define ASMP_CPUFREQ_UP		90
#define ASMP_CPUFREQ_DOWN	60
#define ASMP_CYCLE_UP		1
#define ASMP_CYCLE_DOWN		1

static unsigned int asmp_enabled __read_mostly = 1;

static struct asmp_param_struct {
	unsigned int delay;
	unsigned int max_cpus;
	unsigned int min_cpus;
	unsigned int cpufreq_up;
	unsigned int cpufreq_down;
	unsigned int cycle_up;
	unsigned int cycle_down;
} asmp_param = {
	.delay = ASMP_WORK_DELAY,
	.max_cpus = CONFIG_NR_CPUS,
	.min_cpus = ASMP_MIN_CPUS,
	.cpufreq_up = ASMP_CPUFREQ_UP,
	.cpufreq_down = ASMP_CPUFREQ_DOWN,
	.cycle_up = ASMP_CYCLE_UP,
	.cycle_down = ASMP_CYCLE_DOWN,
};

static struct delayed_work asmp_work;

static struct workqueue_struct *asmp_wq;

static void asmp_work_fn(struct work_struct *work)
{
	unsigned int cpu = 0, slow_cpu = 0, nr_cpu_online;
	unsigned int rate, cpu0_rate, slow_rate = UINT_MAX, fast_rate;
	unsigned int max_rate, up_rate, down_rate, cycle = 0, delay0;
	unsigned long delay_jif;

	if (asmp_enabled) {
	/* Set the work delay */
	cycle++;
	delay0 = asmp_param.delay;
	delay_jif = msecs_to_jiffies(delay0);

	/* Get maximum possible freq for cpu0 and calculate up/down limits */
	get_online_cpus();
	max_rate  = cpufreq_quick_get_max(cpu);
	up_rate   = asmp_param.cpufreq_up * max_rate / 100;
	down_rate = asmp_param.cpufreq_down * max_rate / 100;

	/* Find current max and min cpu freq to estimate load */
	nr_cpu_online = num_online_cpus();
	cpu0_rate = cpufreq_quick_get(cpu);
	fast_rate = cpu0_rate;
	for_each_online_cpu(cpu)
		if (cpu) {
			rate = cpufreq_quick_get(cpu);
			if (rate <= slow_rate) {
				slow_cpu = cpu;
				slow_rate = rate;
			} else if (rate > fast_rate)
				fast_rate = rate;
		}
	put_online_cpus();
	if (cpu0_rate < slow_rate)
		slow_rate = cpu0_rate;

	/* Hotplug one core if all online cores are over up_rate limit */
	if (slow_rate > up_rate) {
		if ((nr_cpu_online < asmp_param.max_cpus) &&
		    (cycle >= asmp_param.cycle_up)) {
			cpu = cpumask_next_zero(0, cpu_online_mask);
			cpu_up(cpu);
			cycle = 0;
		}
	/* Unplug slowest core if all online cores are under down_rate limit */
	} else if (slow_cpu && (fast_rate < down_rate)) {
		if ((nr_cpu_online > asmp_param.min_cpus) &&
			(cycle >= asmp_param.cycle_down)) {
			cpu_down(slow_cpu);
			cycle = 0;
			}
		}
	}
	/* Else do nothing */
	queue_delayed_work(asmp_wq, &asmp_work, delay_jif);
}

static void asmp_early_suspend(struct early_suspend *h)
{
	unsigned int cpu;

	if (!asmp_enabled)
		return;

	/* Offline all sibling cpu cores */
	for_each_present_cpu(cpu) {
		if (cpu && cpu_online(cpu) && num_online_cpus() >
			asmp_param.min_cpus)
			cpu_down(cpu);
	}

	/* Suspend main work thread */
	cancel_delayed_work_sync(&asmp_work);
}

static void asmp_late_resume(struct early_suspend *h)
{
	unsigned int cpu;

	if (!asmp_enabled)
		return;

	/* Online all cpu cores */
	for_each_present_cpu(cpu) {
		if (!cpu_online(cpu) && num_online_cpus() <
			asmp_param.max_cpus)
			cpu_up(cpu);
	}

	/* Resume main work thread */
	queue_delayed_work(asmp_wq, &asmp_work,
				msecs_to_jiffies(asmp_param.delay));
}

static struct early_suspend __refdata asmp_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = asmp_early_suspend,
	.resume = asmp_late_resume,
};

static int set_asmp_enabled(const char *val, const struct kernel_param *kp)
{
	int ret;
	unsigned int cpu;

	ret = param_set_bool(val, kp);

	if (asmp_enabled) {
		queue_delayed_work(asmp_wq, &asmp_work,
				msecs_to_jiffies(asmp_param.delay));
		pr_info("enabled\n");
	} else {
		cancel_delayed_work_sync(&asmp_work);
		for_each_present_cpu(cpu) {
			if (!cpu_online(cpu) && num_online_cpus() <
				asmp_param.max_cpus)
				cpu_up(cpu);
		}
		pr_info("disabled\n");
	}

	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = set_asmp_enabled,
	.get = param_get_bool,
};

module_param_cb(asmp_enabled, &module_ops, &asmp_enabled, 0644);
MODULE_PARM_DESC(asmp_enabled, "hotplug/unplug cpu cores based on cpu load");

/***************************** SYSFS START *****************************/
#define define_one_global_ro(_name)					\
static struct global_attr _name =					\
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_global_rw(_name)					\
static struct global_attr _name =					\
__ATTR(_name, 0644, show_##_name, store_##_name)

struct kobject *asmp_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", asmp_param.object);			\
}
show_one(delay, delay);
show_one(min_cpus, min_cpus);
show_one(max_cpus, max_cpus);
show_one(cpufreq_up, cpufreq_up);
show_one(cpufreq_down, cpufreq_down);
show_one(cycle_up, cycle_up);
show_one(cycle_down, cycle_down);

#define store_one(file_name, object)					\
static ssize_t store_##file_name					\
(struct kobject *a, struct attribute *b, const char *buf, size_t count)	\
{									\
	unsigned int input;						\
	int ret;							\
	ret = sscanf(buf, "%u", &input);				\
	if (ret != 1)							\
		return -EINVAL;						\
	asmp_param.object = input;					\
	return count;							\
}									\
define_one_global_rw(file_name);
store_one(delay, delay);
store_one(min_cpus, min_cpus);
store_one(max_cpus, max_cpus);
store_one(cpufreq_up, cpufreq_up);
store_one(cpufreq_down, cpufreq_down);
store_one(cycle_up, cycle_up);
store_one(cycle_down, cycle_down);

static struct attribute *asmp_attributes[] = {
	&delay.attr,
	&min_cpus.attr,
	&max_cpus.attr,
	&cpufreq_up.attr,
	&cpufreq_down.attr,
	&cycle_up.attr,
	&cycle_down.attr,
	NULL
};

static struct attribute_group asmp_attr_group = {
	.attrs = asmp_attributes,
	.name = "conf",
};
/****************************** SYSFS END ******************************/

static int __init asmp_init(void)
{
	int rc;

	asmp_wq = alloc_workqueue("asmp", WQ_HIGHPRI, 0);
	if (!asmp_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&asmp_work, asmp_work_fn);
	if (asmp_enabled)
		queue_delayed_work(asmp_wq, &asmp_work,
				   msecs_to_jiffies(ASMP_STARTDELAY));

	register_early_suspend(&asmp_early_suspend_handler);

	asmp_kobject = kobject_create_and_add("autosmp", kernel_kobj);
	rc = sysfs_create_group(asmp_kobject, &asmp_attr_group);
	if (rc || !asmp_kobject)
		pr_warn("Failed to create sysfs entry");

	pr_info("initialized\n");

	return 0;
}

static void __exit asmp_exit(void)
{
	unregister_early_suspend(&asmp_early_suspend_handler);
	destroy_workqueue(asmp_wq);
	sysfs_remove_group(asmp_kobject, &asmp_attr_group);
}

late_initcall(asmp_init);
module_exit(asmp_exit);
