#define _XOPEN_SOURCE 700
#include "token.h"
#ifdef TEST_BUILD
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int valid(const char *s, int digits) {
    size_t i, n; int nz = 0;
    if (!s || !(n = strlen(s)) || n > 128) return 0;
    for (i=0;i<n;i++) { unsigned char c=(unsigned char)s[i];
        if (digits) { if(c<'0'||c>'9') return 0; if(c!='0') nz=1; }
        else if (!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'||c=='.'||c=='-')) return 0;
    }
    return !digits || nz;
}
static int write_all(int fd, const char *s) {
    size_t n=strlen(s), done=0; ssize_t r;
    while(done<n) { r=write(fd,s+done,n-done); if(r<0 && errno==EINTR) continue; if(r<=0) return 0; done+=(size_t)r; }
    return fsync(fd)==0;
}
static int same_file(int dir, const char *name, ino_t ino) {
    struct stat st; return fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)==0 && S_ISREG(st.st_mode) && st.st_ino==ino;
}
static void unlink_owned(int dir,const char *name,ino_t ino) { if(same_file(dir,name,ino)) (void)unlinkat(dir,name,0); }
static int exact(int d,const char *name,const char *expected,struct stat *st) {
    int fd=openat(d,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK); char buf[420]; size_t len=strlen(expected),done=0; ssize_t n;
    if(fd<0) return 0;
    if(fstat(fd,st)||!S_ISREG(st->st_mode)||st->st_size!=(off_t)len||len>sizeof buf) { close(fd); return 0; }
    while(done<len) { n=read(fd,buf+done,len-done); if(n<0&&errno==EINTR) continue; if(n<=0) { close(fd); return 0; } done+=(size_t)n; }
    close(fd); return !memcmp(buf,expected,len);
}
static int existing(int d,const char *pid,const char *start,const char *gen) {
    char expected[420]; struct stat st,side;
    if(snprintf(expected,sizeof expected,"%s\n",pid)<0 || !exact(d,"eye-af.resume-ready",expected,&st)) return 0;
    if(snprintf(expected,sizeof expected,"%s %s %lu\n",gen,start,(unsigned long)st.st_ino)<0) return 0;
    return exact(d,"eye-af.resume-ready.identity",expected,&side);
}
static int open_root(const char *root) {
    char *copy,*save,*part,*p; int fd,next;
    copy=strdup(root); if(!copy) return -1;
    fd=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW); p=copy;
    while(fd>=0 && (part=strtok_r(p,"/",&save))) {
        p=NULL;
        if(!strcmp(part,".")||!strcmp(part,"..")) { close(fd); fd=-1; break; }
        next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); close(fd); fd=next;
    }
    free(copy); return fd;
}
int eye_publish_token(const char *root,const char *pid,const char *start,const char *gen,eye_validate_callback cb,void *ctx) {
    char *canonical;
    int r=-1,t=-1,d=-1,lock=-1,tf=-1,sf=-1,ok=0; struct stat ts,ss,ls; char token[140],side[420];
    ino_t ti=0,si=0,li=0; int token_final=0,side_final=0;
    if(!root || root[0]!='/' || !strcmp(root,"/") || !valid(pid,1)||!valid(start,1)||!valid(gen,0)||!cb) return 0;
    canonical=realpath(root,NULL);
    if(!canonical || strcmp(canonical,root) || strncmp(canonical,"/tmp/",5)) { free(canonical); return 0; }
    free(canonical);
    r=open_root(root); if(r<0) goto done;
    t=openat(r,"tmp",O_RDONLY|O_DIRECTORY|O_NOFOLLOW); if(t<0) goto done;
    d=openat(t,"nx-lut-lite",O_RDONLY|O_DIRECTORY|O_NOFOLLOW); if(d<0) goto done;
    if(fstat(d,&ls)||ls.st_uid!=geteuid()||(ls.st_mode & 0022)) goto done;
    lock=openat(d,".eye28.lock",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0700); if(lock<0) goto done;
    if(fstat(lock,&ls)) goto done;
    li=ls.st_ino;
    if(!cb(ctx)) goto done;
    if(faccessat(d,"eye-af.resume-ready",F_OK,AT_SYMLINK_NOFOLLOW)==0) { ok=existing(d,pid,start,gen)&&cb(ctx); goto done; }
    if(faccessat(d,"eye-af.resume-ready.identity",F_OK,AT_SYMLINK_NOFOLLOW)==0) goto done;
    tf=openat(d,".eye28.token.tmp",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600); if(tf<0) goto done;
    if(fstat(tf,&ts)) goto done;
    ti=ts.st_ino;
    sf=openat(d,".eye28.identity.tmp",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600); if(sf<0) goto done;
    if(fstat(sf,&ss)) goto done;
    si=ss.st_ino;
    if(snprintf(token,sizeof token,"%s\n",pid)<0 || snprintf(side,sizeof side,"%s %s %lu\n",gen,start,(unsigned long)ti)<0) goto done;
    if(!write_all(tf,token)||!write_all(sf,side)||!cb(ctx)) goto done;
    /* linkat is atomic and refuses to replace an existing destination. */
    if(linkat(d,".eye28.identity.tmp",d,"eye-af.resume-ready.identity",0)) goto done;
    side_final=1;
    if(linkat(d,".eye28.token.tmp",d,"eye-af.resume-ready",0)) goto done;
    token_final=1;
    if(!cb(ctx)) goto done;
    ok=1;
done:
    if(d>=0) {
      if(!ok && token_final) unlink_owned(d,"eye-af.resume-ready",ti);
      if(!ok && side_final) unlink_owned(d,"eye-af.resume-ready.identity",si);
      if(ti) unlink_owned(d,".eye28.token.tmp",ti);
      if(si) unlink_owned(d,".eye28.identity.tmp",si);
      if(li) unlink_owned(d,".eye28.lock",li);
    }
    if(tf>=0) close(tf);
    if(sf>=0) close(sf);
    if(lock>=0) close(lock);
    if(d>=0) close(d);
    if(t>=0) close(t);
    if(r>=0) close(r);
    return ok;
}
#else
int eye_publish_token(const char *root,const char *pid,const char *start,const char *gen,eye_validate_callback cb,void *ctx) {
    (void)root; (void)pid; (void)start; (void)gen; (void)cb; (void)ctx; return 0;
}
#endif
