#define _POSIX_C_SOURCE 200809L
#include "proof.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

static int valid_id(const char *s, int number) {
    size_t i, n = strlen(s); int nonzero = 0;
    if (!n || n > 128) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (number) {
            if (c < '0' || c > '9') return 0;
            if (c != '0') nonzero = 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return 0;
    }
    if (number) { char *end; errno=0; (void)strtoull(s,&end,10); if (errno || *end) return 0; }
    return !number || nonzero;
}
static int safe_open(const char *filename) {
    char path[4096], *part, *save, *p; int fd, next;
    if (!filename || filename[0]!='/' || strlen(filename)>=sizeof path) return -1;
    strcpy(path, filename); fd=open("/",O_RDONLY|O_DIRECTORY); if(fd<0) return -1;
    p=path;
    while ((part=strtok_r(p,"/",&save))) {
        p=NULL;
        if (!strcmp(part,".") || !strcmp(part,"..")) { close(fd); return -1; }
        next=openat(fd,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(*save ? O_DIRECTORY : 0));
        close(fd); if(next<0) return -1; fd=next;
    }
    return fd;
}
int eye_read_proof(const char *filename, struct eye_proof *out) {
    static const char *const keys[] = {"generation", "image_generation", "boot_id", "pid", "starttime", "build_id", "report_id"};
    struct eye_proof tmp = {0};
    char *values[] = {tmp.generation, tmp.image_generation, tmp.boot_id, tmp.pid, tmp.starttime, tmp.build_id, tmp.report_id};
    char line[260]; unsigned seen = 0; int fd, ok = 1; FILE *f; struct stat st, after;
    if (!filename || !out) return 0;
    fd = safe_open(filename);
    if (fd < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > 2048 || !(f = fdopen(fd, "r"))) { close(fd); return 0; }
    while (fgets(line, sizeof line, f)) {
        char *eq, *nl; size_t i, n = strlen(line);
        if (!n || line[n-1] != '\n') { ok = 0; break; }
        nl = line + n - 1; *nl = 0;
        eq = strchr(line, '='); if (!eq) { ok = 0; break; }
        *eq++ = 0;
        for (i = 0; i < sizeof keys / sizeof keys[0]; ++i) if (!strcmp(line, keys[i])) break;
        if (i == sizeof keys / sizeof keys[0] || (seen & (1u << i)) || !valid_id(eq, i == 3 || i == 4)) { ok = 0; break; }
        strcpy(values[i], eq); seen |= 1u << i;
    }
    if (ferror(f) || fstat(fd,&after) || st.st_dev!=after.st_dev || st.st_ino!=after.st_ino || st.st_size!=after.st_size || st.st_mtim.tv_sec!=after.st_mtim.tv_sec || st.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || st.st_ctim.tv_sec!=after.st_ctim.tv_sec || st.st_ctim.tv_nsec!=after.st_ctim.tv_nsec) ok = 0;
    if (fclose(f)) ok = 0;
    if (!ok || seen != 127u) return 0;
    *out = tmp; return 1;
}
