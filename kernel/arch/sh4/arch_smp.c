/* 
** Copyright 2001-2004, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/
#include <kernel/debug.h>
#include <kernel/smp.h>
#include <kernel/arch/smp.h>

int arch_smp_init(kernel_args *ka)
{
	return 0;
}

int arch_smp_init_percpu(kernel_args *ka, int cpu_num)
{
	return 0;
}

void arch_smp_send_ici(int target_cpu)
{
	panic("called arch_smp_send_ici!\n");
}

void arch_smp_send_broadcast_ici()
{
	panic("called arch_smp_send_broadcast_ici\n");
}

