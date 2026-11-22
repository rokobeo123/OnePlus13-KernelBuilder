#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <dirent.h>

#define DRIVER_IOCTL_READ   0x40047201
#define DRIVER_IOCTL_WRITE  0x40047202
#define DRIVER_IOCTL_MODULE 0x40047203

struct driver_rw {
    int32_t pid;
    int32_t pad;
    uint64_t address;
    uint64_t buffer;
    uint64_t size;
};

struct driver_mod {
    int32_t pid;
    int32_t pad;
    uint64_t name;
    uint64_t base;
};

static int g_fd = -1;

int kmem_init(void) {
    g_fd = open("/dev/kmem_proxy", O_RDWR);
    if (g_fd < 0) {
        perror("[-] Failed to open /dev/kmem_proxy (need root)");
        return -1;
    }
    return 0;
}

void kmem_close(void) {
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

int get_pid_by_name(const char *process_name) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    struct dirent *entry;
    int target_pid = -1;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_DIR) continue;
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char cmdline[256] = {0};
        if (fgets(cmdline, sizeof(cmdline), fp)) {
            if (strstr(cmdline, process_name)) {
                target_pid = pid;
                fclose(fp);
                break;
            }
        }
        fclose(fp);
    }
    closedir(dir);
    return target_pid;
}

uint64_t kmem_get_module_base(int pid, const char *module_name) {
    if (g_fd < 0) return 0;
    struct driver_mod req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.name = (uint64_t)module_name;
    req.base = 0;

    if (ioctl(g_fd, DRIVER_IOCTL_MODULE, &req) < 0) {
        return 0;
    }
    return req.base;
}

int kmem_read(int pid, uint64_t address, void *buffer, size_t size) {
    if (g_fd < 0 || !buffer || size == 0) return -1;
    struct driver_rw req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.address = address;
    req.buffer = (uint64_t)buffer;
    req.size = (uint64_t)size;

    return ioctl(g_fd, DRIVER_IOCTL_READ, &req);
}

int kmem_write(int pid, uint64_t address, const void *buffer, size_t size) {
    if (g_fd < 0 || !buffer || size == 0) return -1;
    struct driver_rw req;
    memset(&req, 0, sizeof(req));
    req.pid = pid;
    req.address = address;
    req.buffer = (uint64_t)buffer;
    req.size = (uint64_t)size;

    return ioctl(g_fd, DRIVER_IOCTL_WRITE, &req);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <process_name> [module_name]\n", argv[0]);
        return 1;
    }

    if (kmem_init() < 0) {
        return 1;
    }

    const char *proc_name = argv[1];
    const char *mod_name = (argc >= 3) ? argv[2] : "libil2cpp.so";

    int pid = get_pid_by_name(proc_name);
    if (pid <= 0) {
        printf("[-] Process '%s' not found\n", proc_name);
        kmem_close();
        return 1;
    }
    printf("[+] Found process '%s' -> PID: %d\n", proc_name, pid);

    uint64_t base = kmem_get_module_base(pid, mod_name);
    if (!base) {
        printf("[-] Module '%s' not found in PID %d\n", mod_name, pid);
    } else {
        printf("[+] Module '%s' Base Address: 0x%lx\n", mod_name, base);

        // Demo read: read first 16 bytes of module ELF header
        uint8_t header[16] = {0};
        if (kmem_read(pid, base, header, sizeof(header)) == 0) {
            printf("[+] ELF Magic: %02x %02x %02x %02x\n", header[0], header[1], header[2], header[3]);
        }
    }

    kmem_close();
    return 0;
}
