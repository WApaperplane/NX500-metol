#define _XOPEN_SOURCE 700
#include "proc_identity.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int directory(int parent, const char *name) { return openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); }
static int rootdir(const char *root) {
    char path[4096], *p, *part, *save; int fd, next;
    if(root[0]!='/' || strlen(root)>=sizeof path) return -1;
    strcpy(path,root); fd=open("/",O_RDONLY|O_DIRECTORY); if(fd<0) return -1;
    p=path;
    while((part=strtok_r(p,"/",&save))) {
        p=NULL; if(!strcmp(part,".")||!strcmp(part,"..")) {close(fd);return -1;}
        next=directory(fd,part); close(fd); if(next<0) return -1; fd=next;
    }
    return fd;
}
static int readfile(int dir, const char *name, char *buf, size_t cap) {
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK); ssize_t n; size_t len=0; struct stat st;
    if(fd<0) return -1;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)) { close(fd); return -1; }
    while(len<cap-1 && (n=read(fd,buf+len,cap-1-len))>0) len+=(size_t)n;
    if(len==cap-1 || n<0 || memchr(buf,0,len)) { close(fd); return -1; }
    close(fd); buf[len]=0;
    while(len && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]=0;
    return len ? 0 : -1;
}
int eye_read_identity(const char *root,long pid,struct eye_identity *out) {
    int r=-1,proc=-1,pd=-1,sys=-1,kernel=-1,random=-1,ret=-1,field; char id[32],b[8192],*end,*q,*closep,*canonical; long parsed; ssize_t n; struct eye_identity tmp;
    if(!root||!out||pid<=0) return -1;
    canonical=realpath(root,NULL);
    if(!canonical||strcmp(root,canonical)) { free(canonical); return -1; }
#ifdef TEST_BUILD
    if(strncmp(canonical,"/tmp/",5)) { free(canonical); return -1; }
#else
    if(strcmp(canonical,"/")) { free(canonical); return -1; }
#endif
    free(canonical);
    r=rootdir(root); if(r<0) goto done;
    proc=directory(r,"proc"); if(proc<0) goto done;
    if(snprintf(id,sizeof id,"%ld",pid)<0) goto done;
    pd=directory(proc,id); if(pd<0 || readfile(pd,"stat",b,sizeof b)) goto done;
    errno=0; parsed=strtol(b,&end,10); if(errno||parsed!=pid||end[0]!=' '||end[1]!='(') goto done;
    closep=strrchr(end+2,')'); if(!closep||closep[1]!=' ') goto done;
    q=closep+2;
    for(field=3;field<=22;field++) {
        char *start=q; while(*q&&*q!=' ') q++; if(q==start) goto done;
        if(field==22) { char save=*q; *q=0; errno=0; if(!isdigit((unsigned char)*start)) goto done;
            tmp.starttime=strtoull(start,&end,10); if(errno||*end||!tmp.starttime) goto done; *q=save; }
        while(*q==' ') q++;
    }
    n=readlinkat(pd,"exe",tmp.exe,sizeof tmp.exe-1); if(n<=0||(size_t)n>=sizeof tmp.exe-1) goto done; tmp.exe[n]=0;
    sys=directory(proc,"sys"); if(sys<0) goto done;
    kernel=directory(sys,"kernel"); if(kernel<0) goto done;
    random=directory(kernel,"random"); if(random<0||readfile(random,"boot_id",tmp.boot_id,sizeof tmp.boot_id)) goto done;
    for(q=tmp.boot_id;*q;q++) if(!isalnum((unsigned char)*q)&&*q!='-') goto done;
    *out=tmp; ret=0;
done:
    if(random>=0) close(random);
    if(kernel>=0) close(kernel);
    if(sys>=0) close(sys);
    if(pd>=0) close(pd);
    if(proc>=0) close(proc);
    if(r>=0) close(r);
    return ret;
}
