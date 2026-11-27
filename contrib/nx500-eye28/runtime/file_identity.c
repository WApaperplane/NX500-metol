#define _GNU_SOURCE
#include "file_identity.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

static int open_safe(const char *root, const char *path) {
    int fd, next; char copy[8192], *p, *part, *save;
    if (!root || root[0]!='/' || !path || path[0]!='/' || !strcmp(root,"/") || snprintf(copy,sizeof copy,"%s%s",root,path)>=(int)sizeof copy) return -1;
    fd=open("/",O_RDONLY|O_DIRECTORY); if(fd<0) return -1;
    p=copy;
    while ((part=strtok_r(p,"/",&save))) {
        p=NULL;
        if (!strcmp(part,".") || !strcmp(part,"..")) { close(fd); return -1; }
        next=openat(fd,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(*save ? O_DIRECTORY : 0));
        close(fd); if(next<0) return -1; fd=next;
    }
    return fd;
}
int eye_verify_file(const char *root,const char *path,const char *expected,struct stat *out) {
    int fd, pipefd[2], status; struct stat a,b; pid_t child; char digest[256], fdpath[64]; ssize_t n,total=0; int i;
    if(!expected || strlen(expected)!=64 || !out) return -1;
    for(i=0;i<64;i++) if(!isxdigit((unsigned char)expected[i]) || isupper((unsigned char)expected[i])) return -1;
    fd=open_safe(root,path); if(fd<0) return -1;
    if(fstat(fd,&a) || !S_ISREG(a.st_mode) || pipe(pipefd)) {close(fd); return -1;}
    child=fork();
    if(child==0) {
        close(pipefd[0]); if(dup2(pipefd[1],STDOUT_FILENO)<0) _exit(127); close(pipefd[1]);
        snprintf(fdpath,sizeof fdpath,"/proc/self/fd/%d",fd);
#ifdef TEST_BUILD
        execl("/usr/bin/sha256sum","sha256sum",fdpath,(char *)0);
#else
        execl("/opt/usr/nx-ks/busybox","busybox","sha256sum",fdpath,(char *)0);
#endif
        _exit(127);
    }
    close(pipefd[1]);
    if(child<0) {close(pipefd[0]);close(fd);return -1;}
    while(total<(ssize_t)sizeof digest) { n=read(pipefd[0],digest+total,sizeof digest-total); if(n<0 && errno==EINTR) continue; if(n<=0) break; total+=n; }
    close(pipefd[0]);
    while(waitpid(child,&status,0)<0) {if(errno!=EINTR){close(fd);return -1;}}
    if(fstat(fd,&b)){close(fd);return -1;} close(fd);
    if(!WIFEXITED(status)||WEXITSTATUS(status)||total<66||total==(ssize_t)sizeof digest || memcmp(digest,expected,64) || digest[64]!=' ' || a.st_dev!=b.st_dev || a.st_ino!=b.st_ino || a.st_size!=b.st_size || a.st_mtim.tv_sec!=b.st_mtim.tv_sec || a.st_mtim.tv_nsec!=b.st_mtim.tv_nsec || a.st_ctim.tv_sec!=b.st_ctim.tv_sec || a.st_ctim.tv_nsec!=b.st_ctim.tv_nsec) return -1;
    *out=b; return 0;
}
int eye_mapping_matches(const char *root,long pid,const char *lib,const struct stat *st) {
    char path[4096], line[8192], perms[5], name[4096]; unsigned long maj,min; unsigned long long ino; FILE *f; int k,found=0;
    if(!root||!lib||!st||pid<=0) return -1;
    k=snprintf(path,sizeof path,"%s/proc/%ld/maps",root,pid); if(k<0||(size_t)k>=sizeof path) return -1;
    k=open_safe(root,path+strlen(root)); if(k<0) return -1;
    f=fdopen(k,"r"); if(!f) { close(k); return -1; }
    while(fgets(line,sizeof line,f)) {
        unsigned long long lo,hi,off; int pos=0;
        if(!strchr(line,'\n') && !feof(f)) { found=0; break; }
        if(sscanf(line,"%llx-%llx %4s %llx %lx:%lx %llu %n",&lo,&hi,perms,&off,&maj,&min,&ino,&pos)!=7 || !pos) continue;
        strncpy(name,line+pos,sizeof name-1); name[sizeof name-1]=0; name[strcspn(name,"\r\n")]=0;
        if(strlen(perms)==4 && perms[2]=='x' && maj==major(st->st_dev) && min==minor(st->st_dev) && ino==(unsigned long long)st->st_ino && !strcmp(name,lib)) {found=1;break;}
    }
    fclose(f); return found?0:-1;
}
