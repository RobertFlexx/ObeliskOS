/*
 * Obelisk OS - sysctl Node Registration
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <sysctl/sysctl.h>
#include <mm/pmm.h>
#include <proc/scheduler.h>

/* ==========================================================================
 * System Information Nodes
 * ========================================================================== */

/* CPU information */
static unsigned int cpu_count = 1;
static unsigned int cpu_frequency = 0;
static char cpu_vendor[64] = "Unknown";
static char cpu_model[128] = "Unknown";

/* Memory information */
static unsigned long mem_cached = 0;

/* Kernel information */
static char hostname[256] = "obelisk";
static char domainname[256] = "";

/* Scheduler information */
static char sched_policy_name[64] = "round-robin";
static unsigned int sched_timeslice = DEF_TIMESLICE;

/* AxiomFS information */
static unsigned int axiomfs_cache_timeout = 100;
static unsigned int axiomfs_cache_size = 65536;
static unsigned int axiomfs_daemon_timeout = 100;
static bool axiomfs_policy_enabled = true;

/* ==========================================================================
 * Dynamic Value Handlers
 * ========================================================================== */

static int sysctl_mem_total_handler(struct sysctl_node *node, void *buf,
                                    size_t *len, loff_t *ppos, bool write) {
    (void)node; (void)ppos;
    
    if (write) {
        return -EPERM;
    }
    
    unsigned long total = pmm_get_usable_pages() * PAGE_SIZE;
    
    if (*len < sizeof(total)) {
        return -EINVAL;
    }
    
    memcpy(buf, &total, sizeof(total));
    *len = sizeof(total);
    
    return 0;
}

static int sysctl_mem_free_handler(struct sysctl_node *node, void *buf,
                                   size_t *len, loff_t *ppos, bool write) {
    (void)node; (void)ppos;
    
    if (write) {
        return -EPERM;
    }
    
    unsigned long free = pmm_get_free_pages() * PAGE_SIZE;
    
    if (*len < sizeof(free)) {
        return -EINVAL;
    }
    
    memcpy(buf, &free, sizeof(free));
    *len = sizeof(free);
    
    return 0;
}

static int sysctl_uptime_handler(struct sysctl_node *node, void *buf,
                                 size_t *len, loff_t *ppos, bool write) {
    (void)node; (void)ppos;
    
    if (write) {
        return -EPERM;
    }
    
    /* get_ticks() currently returns approximate milliseconds. */
    unsigned long up = get_ticks() / 1000;
    
    if (*len < sizeof(up)) {
        return -EINVAL;
    }
    
    memcpy(buf, &up, sizeof(up));
    *len = sizeof(up);
    
    return 0;
}

static int sysctl_context_switches_handler(struct sysctl_node *node, void *buf,
                                           size_t *len, loff_t *ppos, bool write) {
    (void)node; (void)ppos;
    
    if (write) {
        return -EPERM;
    }
    
    unsigned long switches = scheduler_get_switches();
    
    if (*len < sizeof(switches)) {
        return -EINVAL;
    }
    
    memcpy(buf, &switches, sizeof(switches));
    *len = sizeof(switches);
    
    return 0;
}

static int sysctl_hostname_handler(struct sysctl_node *node, void *buf,
                                   size_t *len, loff_t *ppos, bool write) {
    (void)node; (void)ppos;
    
    if (write) {
        size_t copylen = MIN(*len, sizeof(hostname) - 1);
        memcpy(hostname, buf, copylen);
        hostname[copylen] = '\0';
        return 0;
    }
    
    size_t hostlen = strlen(hostname) + 1;
    if (*len < hostlen) {
        return -EINVAL;
    }
    
    memcpy(buf, hostname, hostlen);
    *len = hostlen;
    
    return 0;
}

/* ==========================================================================
 * Node Registration
 * ========================================================================== */

void sysctl_register_system_nodes(void) {
    /* CPU nodes */
    sysctl_register_uint("system.cpu.count", &cpu_count, SYSCTL_RO);
    sysctl_register_uint("system.cpu.frequency", &cpu_frequency, SYSCTL_RO);
    sysctl_register_string("system.cpu.vendor", cpu_vendor, 
                          sizeof(cpu_vendor), SYSCTL_RO);
    sysctl_register_string("system.cpu.model", cpu_model,
                          sizeof(cpu_model), SYSCTL_RO);
    
    /* Memory nodes (dynamic) */
    sysctl_register_handler("system.memory.total", sysctl_mem_total_handler,
                           NULL, SYSCTL_RO);
    sysctl_register_handler("system.memory.free", sysctl_mem_free_handler,
                           NULL, SYSCTL_RO);
    sysctl_register_ulong("system.memory.cached", &mem_cached, SYSCTL_RO);
    
    /* Kernel nodes */
    sysctl_register_handler("system.kernel.hostname", sysctl_hostname_handler,
                           NULL, SYSCTL_RW);
    sysctl_register_string("system.kernel.domainname", domainname,
                          sizeof(domainname), SYSCTL_RW);
    sysctl_register_handler("system.kernel.uptime", sysctl_uptime_handler,
                           NULL, SYSCTL_RO);
    sysctl_register_handler("system.kernel.context_switches", 
                           sysctl_context_switches_handler, NULL, SYSCTL_RO);
    
    /* Scheduler nodes */
    sysctl_register_string("system.scheduler.policy", sched_policy_name,
                          sizeof(sched_policy_name), SYSCTL_RO);
    sysctl_register_uint("system.scheduler.timeslice", &sched_timeslice, 
                        SYSCTL_RW);
    
    /* AxiomFS nodes */
    sysctl_register_uint("system.fs.axiomfs.cache_timeout", 
                        &axiomfs_cache_timeout, SYSCTL_RW);
    sysctl_register_uint("system.fs.axiomfs.cache_size",
                        &axiomfs_cache_size, SYSCTL_RW);
    sysctl_register_uint("system.fs.axiomfs.daemon_timeout",
                        &axiomfs_daemon_timeout, SYSCTL_RW);
    sysctl_register_bool("system.fs.axiomfs.policy_enabled",
                        &axiomfs_policy_enabled, SYSCTL_RW);
}

/* Initialize CPU information */
void sysctl_init_cpu_info(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* Get vendor string */
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    memcpy(cpu_vendor, &ebx, 4);
    memcpy(cpu_vendor + 4, &edx, 4);
    memcpy(cpu_vendor + 8, &ecx, 4);
    cpu_vendor[12] = '\0';
    
    /* Get brand string if available */
    cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000004) {
        uint32_t *p = (uint32_t *)cpu_model;
        
        cpuid(0x80000002, 0, &p[0], &p[1], &p[2], &p[3]);
        cpuid(0x80000003, 0, &p[4], &p[5], &p[6], &p[7]);
        cpuid(0x80000004, 0, &p[8], &p[9], &p[10], &p[11]);
        cpu_model[48] = '\0';
        
        /* Trim leading spaces */
        char *s = cpu_model;
        while (*s == ' ') s++;
        if (s != cpu_model) {
            memmove(cpu_model, s, strlen(s) + 1);
        }
    }
}