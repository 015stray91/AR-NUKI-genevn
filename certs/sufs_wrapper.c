/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <compiler.h>
#include <kpmodule.h>
#include <linux/printk.h>
#include <common.h>
#include <kputils.h>
#include <linux/string.h>

// Module metadata
KPM_NAME("sufs-module");
KPM_VERSION("1.0.0");
KPM_LICENSE("GPL v2");
KPM_AUTHOR("Your Name");
KPM_DESCRIPTION("SUFS filesystem module");

// Forward declarations from your original module
extern int sufs_init(void);
extern void sufs_exit(void);

/**
 * @brief KPM initialization - called when module loads
 * @param args Command-line arguments passed from KPM installer
 * @param event Boot stage (e.g., "pre-kernel-init", "post-kernel-init")
 * @param reserved Reserved for future use
 */
static long sufs_kpm_init(const char *args, const char *event, void *__user reserved)
{
    pr_info("[SUFS-KPM] Module initializing at event: %s\n", event);
    pr_info("[SUFS-KPM] Arguments: %s\n", args);
    
    // Call your original module initialization
    int ret = sufs_init();
    if (ret) {
        pr_err("[SUFS-KPM] Initialization failed: %d\n", ret);
        return ret;
    }
    
    pr_info("[SUFS-KPM] Module loaded successfully\n");
    return 0;
}

/**
 * @brief KPM control command 0 - for passing configuration
 */
static long sufs_kpm_control0(const char *ctl_args, char *__user out_msg, int outlen)
{
    pr_info("[SUFS-KPM] Control command received: %s\n", ctl_args);
    // Handle control commands here
    return 0;
}

/**
 * @brief KPM cleanup - called when module unloads
 */
static long sufs_kpm_exit(void *__user reserved)
{
    pr_info("[SUFS-KPM] Module unloading\n");
    sufs_exit();
    return 0;
}

// Register KPM entry points
KPM_INIT(sufs_kpm_init);
KPM_CTL0(sufs_kpm_control0);
KPM_EXIT(sufs_kpm_exit);