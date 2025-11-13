#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

#define RAM_SPIKE_THRESHOLD 30.0
#define MONITOR_ITERATIONS 60
#define HIGH_RAM_THRESHOLD 80.0  
#define SYSCALL_SPIKE_THRESHOLD 1000 
#define NETWORK_SPIKE_THRESHOLD 1000000

typedef struct {
    unsigned long long total_memory;
    unsigned long long used_memory;
    double usage_percent;
} MemoryStats;

typedef struct {
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
    unsigned long long rx_packets;
    unsigned long long tx_packets;
} NetworkStats;

typedef struct {
    unsigned long long total_syscalls;
    unsigned long long open_calls;
    unsigned long long exec_calls;
    unsigned long long fork_calls;
} SyscallStats;

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

int get_network_stats_from_vm(const char *vm_ip, const char *vm_user, NetworkStats *stats) {
    char cmd[512];
    FILE *fp;
    const char *ssh_opts = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5";
    
    // Get first non-loopback interface stats from /proc/net/dev
    snprintf(cmd, sizeof(cmd),
             "timeout 10 ssh %s %s@%s "
             "'cat /proc/net/dev | grep -v \"lo:\" | grep \":\" | head -1 | awk \"{print \\$2,\\$3,\\$10,\\$11}\"'",
             ssh_opts, vm_user, vm_ip);
    
    fp = popen(cmd, "r");
    if (fp == NULL) return -1;
    
    if (fscanf(fp, "%llu %llu %llu %llu", 
               &stats->rx_bytes, &stats->rx_packets,
               &stats->tx_bytes, &stats->tx_packets) != 4) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    
    return 0;
}

int get_syscall_stats_from_vm(const char *vm_ip, const char *vm_user, SyscallStats *stats) {
    char cmd[512];
    FILE *fp;
    const char *ssh_opts = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5";
    
    // Count processes and estimate syscall activity
    snprintf(cmd, sizeof(cmd),
             "timeout 10 ssh %s %s@%s "
             "'ps aux | wc -l; cat /proc/stat | grep \"^processes\" | awk \"{print \\$2}\"; "
             "cat /proc/stat | grep \"^procs_running\" | awk \"{print \\$2}\"'",
             ssh_opts, vm_user, vm_ip);
    
    fp = popen(cmd, "r");
    if (fp == NULL) return -1;
    
    unsigned long long proc_count = 0, total_procs = 0, running_procs = 0;
    if (fscanf(fp, "%llu %llu %llu", &proc_count, &total_procs, &running_procs) != 3) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    
    stats->total_syscalls = total_procs;
    stats->fork_calls = (proc_count > 2) ? (proc_count - 2) : 0;
    stats->exec_calls = running_procs;
    stats->open_calls = proc_count * 3;  // Rough estimate
    
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
        printf("[INFO] VM started! Waiting 5s...\n");
        sleep(5);
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
    NetworkStats net_stats;
    NetworkStats prev_net_stats = {0, 0, 0, 0};
    SyscallStats syscall_stats;
    SyscallStats prev_syscall_stats = {0, 0, 0, 0};
    int is_malicious = 0;
    int spike_count = 0;
    int net_spike_count = 0;
    int syscall_spike_count = 0;
    
    printf("==============================================\n");
    printf("Starting Comprehensive Monitoring (2 minutes)\n");
    printf("==============================================\n\n");
    
    if (get_memory_stats_from_vm(vm_ip, vm_user, &prev_stats) < 0) {
        fprintf(stderr, "[ERROR] Failed to get baseline memory stats\n");
        return -1;
    }
    
    if (get_network_stats_from_vm(vm_ip, vm_user, &prev_net_stats) < 0) {
        fprintf(stderr, "[WARN] Failed to get baseline network stats\n");
    }
    
    if (get_syscall_stats_from_vm(vm_ip, vm_user, &prev_syscall_stats) < 0) {
        fprintf(stderr, "[WARN] Failed to get baseline syscall stats\n");
    }
    
    printf("[BASELINE] Memory: %.2f MB (%.1f%%) | Network: RX %.2f MB | Syscalls: %llu\n\n", 
           prev_stats.used_memory / 1024.0, prev_stats.usage_percent,
           prev_net_stats.rx_bytes / (1024.0 * 1024.0), prev_syscall_stats.total_syscalls);
    
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
                printf("\n[TERMINATED] VM crash detected - Program exiting\n\n");
                exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
            }
            continue;
        }
        
        consecutive_failures = 0;  // Reset on success
        
        // Get network and syscall stats (non-fatal if they fail, use previous values)
        if (get_network_stats_from_vm(vm_ip, vm_user, &net_stats) < 0) {
            net_stats = prev_net_stats;  // Use previous values on failure
        }
        if (get_syscall_stats_from_vm(vm_ip, vm_user, &syscall_stats) < 0) {
            syscall_stats = prev_syscall_stats;  // Use previous values on failure
        }
        
        // Calculate memory changes
        long long mem_change_kb = (long long)stats.used_memory - (long long)prev_stats.used_memory;
        double mem_change_mb = mem_change_kb / 1024.0;
        
        double percent_change = 0.0;
        if (prev_stats.used_memory > 10240) {  // Only calculate if prev > 10MB
            percent_change = ((double)mem_change_kb / (double)prev_stats.used_memory) * 100.0;
        }
        
        // Calculate network changes (bytes transferred)
        long long net_rx_change = (long long)net_stats.rx_bytes - (long long)prev_net_stats.rx_bytes;
        long long net_tx_change = (long long)net_stats.tx_bytes - (long long)prev_net_stats.tx_bytes;
        
        // Calculate syscall changes
        long long syscall_change = (long long)syscall_stats.total_syscalls - (long long)prev_syscall_stats.total_syscalls;
        long long fork_change = (long long)syscall_stats.fork_calls - (long long)prev_syscall_stats.fork_calls;
        
        // Display stats
        printf("[%03d] RAM: %.2f MB (%.1f%%) %+.1f MB", 
               i+1, 
               stats.used_memory / 1024.0, 
               stats.usage_percent,
               mem_change_mb);
        
        printf(" | NET: RX %+.2f KB TX %+.2f KB", 
               net_rx_change / 1024.0, net_tx_change / 1024.0);
        
        printf(" | SYS: %llu procs %+lld forks", 
               syscall_stats.total_syscalls, fork_change);
        
        // Detect memory spike: only on INCREASE >30% OR >100MB sudden increase
        if ((percent_change > RAM_SPIKE_THRESHOLD && mem_change_kb > 0 && prev_stats.used_memory > 10240) || 
            mem_change_mb > 100.0) {
            printf(" ⚠️  RAM-SPIKE!");
            is_malicious = 1;
            spike_count++;
        }
        
        // Detect network spike: >1MB/s change (>500KB in 2 seconds)
        if (net_rx_change > NETWORK_SPIKE_THRESHOLD / 2 || net_tx_change > NETWORK_SPIKE_THRESHOLD / 2) {
            printf(" ⚠️  NET-SPIKE!");
            is_malicious = 1;
            net_spike_count++;
        }
        
        // Detect syscall spike: rapid fork activity (>50 forks in 2 seconds)
        if (fork_change > 50 || syscall_change > SYSCALL_SPIKE_THRESHOLD) {
            printf(" ⚠️  SYSCALL-SPIKE!");
            is_malicious = 1;
            syscall_spike_count++;
        }
        
        // Check if RAM usage is critically high - IMMEDIATE TERMINATION
        if (stats.usage_percent > HIGH_RAM_THRESHOLD) {
            printf(" 🔴 CRITICAL!");
            printf("\n\n🚨 STOPPING VM - MALICIOUS BEHAVIOR CONFIRMED! 🚨\n");
            printf("   - RAM usage exceeded %.0f%%\n", HIGH_RAM_THRESHOLD);
            printf("   - %d RAM spike(s), %d network spike(s), %d syscall spike(s)\n", 
                   spike_count, net_spike_count, syscall_spike_count);
            printf("\n[ACTION] Stopping VM and terminating...\n");
            stop_vm(vm_name);
            printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
            exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
        }
        
        // If we've accumulated multiple spikes, stop immediately
        if (spike_count >= 3 || net_spike_count >= 3 || syscall_spike_count >= 3) {
            printf("\n\n🚨 STOPPING VM - MALICIOUS BEHAVIOR CONFIRMED! 🚨\n");
            printf("   - Multiple anomalies detected (%d RAM, %d network, %d syscall)\n", 
                   spike_count, net_spike_count, syscall_spike_count);
            printf("   - Sustained attack pattern identified\n");
            printf("\n[ACTION] Stopping VM and terminating...\n");
            stop_vm(vm_name);
            printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
            exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
        }
        
        printf("\n");
        prev_stats = stats;
        prev_net_stats = net_stats;
        prev_syscall_stats = syscall_stats;
    }
    
    printf("\n==============================================\n");
    printf("Monitoring Complete\n");
    printf("==============================================\n");
    
    if (is_malicious) {
        printf("\n🚨 WARNING: MALICIOUS BEHAVIOR DETECTED! 🚨\n");
        printf("   - %d RAM spike(s) detected (>%.0f%% increase)\n", spike_count, RAM_SPIKE_THRESHOLD);
        printf("   - %d network spike(s) detected (>%.2f MB/s)\n", net_spike_count, NETWORK_SPIKE_THRESHOLD / (1024.0 * 1024.0));
        printf("   - %d syscall spike(s) detected (fork bombs, process spawning)\n", syscall_spike_count);
        printf("   - Abnormal system behavior\n");
        printf("   - Possible fork bomb, memory attack, or data exfiltration\n");
        printf("\n[ACTION] Stopping VM and terminating...\n");
        stop_vm(vm_name);
        printf("\n[TERMINATED] Malicious file detected - Program exiting\n\n");
        exit(EXIT_FAILURE);  // TERMINATE IMMEDIATELY
    } else {
        printf("\n✓ No suspicious behavior detected\n");
        printf("   - Memory usage remained stable\n");
        printf("   - Network activity was normal\n");
        printf("   - Syscall activity was normal\n");
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

