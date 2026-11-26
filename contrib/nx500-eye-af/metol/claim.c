/* Single-use capture claim. Build for camera ARM; never run on a live camera for testing. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    struct stat st; char path[512], body[1024]; int fd, n;
    if (argc != 7 || strcmp(argv[5], "EYE") || strspn(argv[4], "0123456789") != strlen(argv[4]) || !*argv[4]) return 1;
    if (strchr(argv[2], '\n') || strchr(argv[3], '\n') || strchr(argv[6], '\n')) return 1;
    if (lstat(argv[6], &st) || !S_ISREG(st.st_mode)) return 1;
    if (snprintf(path, sizeof path, "%s/launch-claim", argv[1]) >= (int)sizeof path) return 1;
    n = snprintf(body, sizeof body, "boot_id=%s\nmode=%s\ncamera_pid=%s\npolicy=%s\nfile=%s\nmapped_dev=%02x:%02x\nmapped_ino=%llu\n", argv[2], argv[3], argv[4], argv[5], argv[6], major(st.st_dev), minor(st.st_dev), (unsigned long long)st.st_ino);
    if (n < 0 || n >= (int)sizeof body) return 1;
    fd = open(path, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0600); if (fd < 0) return 1;
    if (write(fd, body, n) != n || fsync(fd) || close(fd)) { unlink(path); return 1; }
    return 0;
}
