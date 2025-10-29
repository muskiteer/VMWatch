#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

#define RAM_SPIKE_THRESHOLD 30.0
#define MONITOR_ITERATIONS 60
#define HIGH_RAM_THRESHOLD 80.0  // Stop if RAM usage exceeds 80%

typedef struct {
    unsigned long long total_memory;
    unsigned long long used_memory;
    double usage_percent;
} MemoryStats;

int get_memory_stats_from_vm(const char *vm_ip, const char *vm_user, MemoryStats *stats) {
    char cmd[512];
    FILE *fp;
    const char *ssh_opts = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5";
    
    // Get memory info from inside the VM using /proc/meminfo
    snprintf(cmd, sizeof(cmd), 
             "timeout 10 ssh %s %s@%s "
             "'cat /proc/meminfo | grep -E \"^(MemTotal|MemAvailable):\" | awk \"{print \\$2}\"'",
             ssh_opts, vm_user, vm_ip);
    
    fp = popen(cmd, "r");
    if (fp == NULL) return -1;
    
    unsigned long long mem_total = 0, mem_available = 0;
    if (fscanf(fp, "%llu", &mem_total) != 1) {
        pclose(fp);
        return -1;
    }
    if (fscanf(fp, "%llu", &mem_available) != 1) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    
    stats->total_memory = mem_total;
    stats->used_memory = mem_total - mem_available;
    
    if (stats->total_memory > 0) {
        stats->usage_percent = (double)stats->used_memory / stats->total_memory * 100.0;
    }
    
    return 0;
}

int get_memory_stats(const char *vm_name, MemoryStats *stats) {
    // This is kept for backward compatibility but not used
    // We now get stats directly from VM
    (void)vm_name;
    (void)stats;
    return -1;
}

int start_vm(const char *vm_name) {
    virConnectPtr conn;
    virDomainPtr dom;
    int ret;

    printf("[INFO] Connecting to QEMU hypervisor...\n");
    conn = virConnectOpen("qemu:///system");
    if (conn == NULL) {
        fprintf(stderr, "[ERROR] Failed to open connection\n");
        return -1;
    }

    printf("[INFO] Looking up VM: %s\n", vm_name);
    dom = virDomainLookupByName(conn, vm_name);
    if (dom == NULL) {
        fprintf(stderr, "[ERROR] VM not found\n");
        virConnectClose(conn);
        return -1;
    }

    if (virDomainIsActive(dom) == 1) {
        printf("[INFO] VM already running\n");
    } else {
        printf("[INFO] Starting VM...\n");
        ret = virDomainCreate(dom);
        if (ret < 0) {
            fprintf(stderr, "[ERROR] Failed to start VM\n");
            virDomainFree(dom);
            virConnectClose(conn);
            return -1;
        }
        printf("[INFO] VM started! Waiting 30s...\n");
        sleep(30);
    }

    virDomainFree(dom);
    virConnectClose(conn);
    return 0;
}

int stop_vm(const char *vm_name) {
    virConnectPtr conn;
    virDomainPtr dom;

    conn = virConnectOpen("qemu:///system");
    if (conn == NULL) {
        fprintf(stderr, "[ERROR] Failed to connect to stop VM\n");
        return -1;
    }

    dom = virDomainLookupByName(conn, vm_name);
    if (dom == NULL) {
        fprintf(stderr, "[ERROR] Failed to find VM to stop\n");
        virConnectClose(conn);
        return -1;
    }

    printf("\n[ACTION] Stopping VM '%s' due to malicious behavior...\n", vm_name);
    if (virDomainDestroy(dom) < 0) {
        fprintf(stderr, "[ERROR] Failed to stop VM\n");
        virDomainFree(dom);
        virConnectClose(conn);
        return -1;
    }

    printf("[ACTION] VM stopped successfully!\n");
    virDomainFree(dom);
    virConnectClose(conn);
    return 0;
}

int run_script_in_vm(const char *script_path, const char *vm_ip, const char *vm_user) {
    char cmd[512];
    const char *ssh_opts = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR";
    
    printf("\n[INFO] Copying script to VM...\n");
    snprintf(cmd, sizeof(cmd), "scp %s %s %s@%s:/tmp/script.sh", 
             ssh_opts, script_path, vm_user, vm_ip);
    if (system(cmd) != 0) {
        fprintf(stderr, "[ERROR] Failed to copy script\n");
        return -1;
    }
    
    printf("[INFO] Making executable...\n");
    snprintf(cmd, sizeof(cmd), "ssh %s %s@%s 'chmod +x /tmp/script.sh'", 
             ssh_opts, vm_user, vm_ip);
    if (system(cmd) != 0) {
        fprintf(stderr, "[WARN] Failed to make script executable\n");
    }
    
    printf("[INFO] Executing script and capturing output...\n");
    snprintf(cmd, sizeof(cmd), "ssh %s %s@%s '/tmp/script.sh > /tmp/script_output.log 2>&1 &'", 
             ssh_opts, vm_user, vm_ip);
    if (system(cmd) != 0) {
        fprintf(stderr, "[WARN] Script execution may have failed\n");
    }
    
    printf("[INFO] Script started! Output logged to /tmp/script_output.log in VM\n\n");
    return 0;
}

int monitor_and_detect(const char *vm_name, const char *vm_ip, const char *vm_user) {
    MemoryStats stats;
    MemoryStats prev_stats = {0, 0, 0.0};
    int is_malicious = 0;
    int spike_count = 0;
    
    printf("==============================================\n");
    printf("Starting RAM Monitoring (2 minutes)\n");
    printf("==============================================\n\n");
    
    if (get_memory_stats_from_vm(vm_ip, vm_user, &prev_stats) < 0) {
        fprintf(stderr, "[ERROR] Failed to get baseline\n");
        return -1;
    }
    
    printf("[BASELINE] Used: %.2f MB (%.1f%%)\n\n", 
           prev_stats.used_memory / 1024.0, prev_stats.usage_percent);
    
    int consecutive_failures = 0;
    
    for (int i = 0; i < MONITOR_ITERATIONS; i++) {
        sleep(2);
        
        if (get_memory_stats_from_vm(vm_ip, vm_user, &stats) < 0) {
            consecutive_failures++;
            fprintf(stderr, "[WARN] Failed at iteration %d (failures: %d)\n", i+1, consecutive_failures);
            
            // If we get 3 consecutive failures, VM is likely crashed
            if (consecutive_failures >= 3) {
                printf("\n\n🚨 VM CRASHED - MALICIOUS BEHAVIOR CONFIRMED! 🚨\n");
                printf("   - Could not connect for 3 consecutive attempts\n");
                printf("   - Script caused complete system crash\n");
                printf("   - This is EXTREMELY DANGEROUS behavior\n");
                printf("   - %d memory spike(s) detected before crash\n", spike_count);
                printf("\n[ACTION] Cleaning up crashed VM...\n");
                stop_vm(vm_name);
                return 1;
            }
            continue;
        }
        
        consecutive_failures = 0;  // Reset on success
        
        // Calculate absolute change in MB
        long long mem_change_kb = (long long)stats.used_memory - (long long)prev_stats.used_memory;
        double mem_change_mb = mem_change_kb / 1024.0;
        
        // Calculate percentage change (only if previous is significant to avoid division issues)
        double percent_change = 0.0;
        if (prev_stats.used_memory > 10240) {  // Only calculate if prev > 10MB
            percent_change = ((double)mem_change_kb / (double)prev_stats.used_memory) * 100.0;
        }
        
        printf("[%03d] Used: %.2f MB (%.1f%%) | Change: %+.1f MB (%+.1f%%)", 
               i+1, 
               stats.used_memory / 1024.0, 
               stats.usage_percent,
               mem_change_mb,
               percent_change);
        
        // Detect spike: only on INCREASE >30% OR >100MB sudden increase
        if ((percent_change > RAM_SPIKE_THRESHOLD && mem_change_kb > 0 && prev_stats.used_memory > 10240) || 
            mem_change_mb > 100.0) {
            printf(" ⚠️  SPIKE DETECTED!");
            is_malicious = 1;
            spike_count++;
        }
        
        // Check if RAM usage is critically high
        if (stats.usage_percent > HIGH_RAM_THRESHOLD) {
            printf(" 🔴 CRITICAL RAM (%.1f%%)!", stats.usage_percent);
            printf("\n\n🚨 STOPPING VM - MALICIOUS BEHAVIOR CONFIRMED! 🚨\n");
            printf("   - RAM usage exceeded %.0f%%\n", HIGH_RAM_THRESHOLD);
            printf("   - %d spike(s) detected\n", spike_count);
            stop_vm(vm_name);
            return 1;
        }
        
        printf("\n");
        prev_stats = stats;
    }
    
    printf("\n==============================================\n");
    printf("Monitoring Complete\n");
    printf("==============================================\n");
    
    if (is_malicious) {
        printf("\n🚨 WARNING: POTENTIALLY MALICIOUS BEHAVIOR! 🚨\n");
        printf("   - %d RAM spike(s) detected (>%.0f%% increase)\n", spike_count, RAM_SPIKE_THRESHOLD);
        printf("   - Abnormal memory consumption\n");
        printf("   - Possible fork bomb or memory attack\n");
        printf("\n[ACTION] Stopping VM due to malicious behavior...\n");
        stop_vm(vm_name);
        return 1;
    } else {
        printf("\n✓ No suspicious behavior detected\n");
        return 0;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <vm-name> <vm-ip> <script-path>\n", argv[0]);
        fprintf(stderr, "Example: %s example-vm 192.168.122.100 ./test.sh\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *vm_name = argv[1];
    const char *vm_ip = argv[2];
    const char *script_path = argv[3];
    const char *vm_user = "ubuntu";

    printf("\n╔════════════════════════════════════════════╗\n");
    printf("║       VMWatch - Security Monitor          ║\n");
    printf("╚════════════════════════════════════════════╝\n\n");
    printf("VM: %s | IP: %s | Script: %s\n\n", vm_name, vm_ip, script_path);

    if (start_vm(vm_name) < 0) {
        fprintf(stderr, "\n[FATAL] Failed to start VM\n");
        return EXIT_FAILURE;
    }

    if (run_script_in_vm(script_path, vm_ip, vm_user) < 0) {
        fprintf(stderr, "\n[FATAL] Failed to run script\n");
        return EXIT_FAILURE;
    }

    printf("Waiting 5 seconds for script to initialize...\n\n");
    sleep(5);
    
    int result = monitor_and_detect(vm_name, vm_ip, vm_user);
    
    // Fetch and display script output
    printf("\n==============================================\n");
    printf("Script Output from VM\n");
    printf("==============================================\n");
    char cmd[512];
    const char *ssh_opts = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR";
    snprintf(cmd, sizeof(cmd), "ssh %s %s@%s 'cat /tmp/script_output.log 2>/dev/null || echo \"[No output captured]\"'", 
             ssh_opts, vm_user, vm_ip);
    if (system(cmd) != 0) {
        fprintf(stderr, "[WARN] Failed to fetch script output\n");
    }
    
    printf("\n");
    return result;
}

