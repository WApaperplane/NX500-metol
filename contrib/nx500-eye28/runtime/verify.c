#include <stdio.h>
#ifdef TEST_BUILD
#define _XOPEN_SOURCE 700
#include "proc_identity.h"
#include "file_identity.h"
#include "proof.h"
#include "token.h"
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef FIXTURE_SHA
#define FIXTURE_SHA ""
#endif
static const char *paths[]={"/opt/App","/opt/eye.so","/opt/libface.so","/opt/libcapture.so"};
struct context { const char *root; long pid; struct eye_identity id; struct eye_proof proof; };
static int read_safe(const char *root,const char *path,char *buf,size_t cap) {
    int fd=-1,nfd=-1; char combined[8192],*s,*part,*save; size_t used=0; ssize_t n; struct stat st;
    if(snprintf(combined,sizeof combined,"%s%s",root,path)>=(int)sizeof combined) return 0;
    fd=open("/",O_RDONLY|O_DIRECTORY); if(fd<0) return 0;
    s=combined;
    while((part=strtok_r(s,"/",&save))) {
        s=NULL; if(!strcmp(part,".")||!strcmp(part,"..")) goto bad;
        nfd=openat(fd,part,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(*save?O_DIRECTORY:0));
        close(fd); fd=nfd; if(fd<0) return 0;
    }
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)) goto bad;
    while(used<cap-1 && (n=read(fd,buf+used,cap-1-used))>0) used+=(size_t)n;
    if(used==cap-1||n<0||memchr(buf,0,used)) goto bad;
    buf[used]=0; close(fd); return 1;
bad: if(fd>=0) close(fd); return 0;
}
static int root_ok(const char *root) {
    char *p=realpath(root,NULL); int ok=p && !strcmp(p,root) && !strncmp(p,"/tmp/",5); free(p); return ok;
}
static int proof_read(const char *root,struct eye_proof *p) {
    char path[4096];
    if(snprintf(path,sizeof path,"%s/proof",root)>=(int)sizeof path) return 0;
    return eye_read_proof(path,p);
}
static int number(const char *s,int signed_value) {
    if(signed_value && *s=='-') s++;
    if(!*s) return 0;
    for(;*s;s++) if(*s<'0'||*s>'9') return 0;
    return 1;
}
static int evidence(const char *root,int resume) {
    char b[8192],*line,*save,*field,*fs,*fields[6]; int swap=0,first=1,i;
    if(!read_safe(root,"/run/eye28.bootmode",b,sizeof b)||strcmp(b,"capture\n")) return 0;
    if(!read_safe(root,"/proc/swaps",b,sizeof b) || b[0]=='\n' || strstr(b,"\n\n")) return 0;
    for(line=strtok_r(b,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        i=0;
        for(field=strtok_r(line," \t\r",&fs);field && i<6;field=strtok_r(NULL," \t\r",&fs)) fields[i++]=field;
        if(i!=5 || field) return 0;
        if(first) {
            if(strcmp(fields[0],"Filename")||strcmp(fields[1],"Type")||strcmp(fields[2],"Size")||strcmp(fields[3],"Used")||strcmp(fields[4],"Priority")) return 0;
            first=0; continue;
        }
        if(!number(fields[2],0)||!number(fields[3],0)||!number(fields[4],1)) return 0;
        if(!strcmp(fields[0],"/dev/mmcblk0p9")) swap=1;
    }
    if(first) return 0;
    if(resume && !read_safe(root,"/tmp/.hib_post",b,sizeof b)) return 0;
    return resume ? !swap : swap;
}
static int files(const char *root,long pid,int maps) {
    size_t i; struct stat st;
    for(i=0;i<sizeof paths/sizeof paths[0];i++) {
        if(eye_verify_file(root,paths[i],FIXTURE_SHA,&st)) return 0;
        if(maps && eye_mapping_matches(root,pid,paths[i],&st)) return 0;
    }
    return 1;
}
static int proof_base(const char *root,struct eye_proof *p) {
    char b[256]; size_t n;
    if(!proof_read(root,p)||strcmp(p->generation,p->image_generation)||strcmp(p->build_id,"fixture-build")||strcmp(p->report_id,"fixture-report")) return 0;
    if(!read_safe(root,"/proc/sys/kernel/random/boot_id",b,sizeof b)) return 0;
    n=strlen(b); if(n && b[n-1]=='\n') b[n-1]=0;
    return !strcmp(b,p->boot_id);
}
static int unchanged(void *v) {
    struct context *c=v; struct eye_identity id; struct eye_proof p;
    return proof_base(c->root,&p) && !memcmp(&p,&c->proof,sizeof p) && evidence(c->root,1) && files(c->root,c->pid,1) &&
        !eye_read_identity(c->root,c->pid,&id) && id.starttime==c->id.starttime && !strcmp(id.exe,c->id.exe) && !strcmp(id.boot_id,c->id.boot_id);
}
static int run(const char *op,const char *root,long pid) {
    struct eye_proof proof; struct eye_identity id; struct context ctx; char start[32],ps[32];
    if(!root_ok(root)||!proof_base(root,&proof)) return 0;
    if(!strcmp(op,"launch")) {
        struct eye_proof again;
        return evidence(root,0)&&files(root,pid,0)&&proof_base(root,&again)&&
               !memcmp(&proof,&again,sizeof proof)&&evidence(root,0)&&files(root,pid,0);
    }
    if(strcmp(op,"resume")||pid<=0||!evidence(root,1)||!files(root,pid,1)||eye_read_identity(root,pid,&id)) return 0;
    snprintf(start,sizeof start,"%llu",id.starttime); snprintf(ps,sizeof ps,"%ld",pid);
    if(strcmp(proof.pid,ps)||strcmp(proof.starttime,start)||strcmp(proof.boot_id,id.boot_id)||strcmp(id.exe,"/opt/App")) return 0;
    ctx.root=root; ctx.pid=pid; ctx.id=id; ctx.proof=proof;
#ifdef EYE_TEST_FAULT_INJECTION
    {
        const char *fault=getenv("EYE_TEST_REMOVE"); char target[4096];
        if(fault && (!strcmp(fault,"proof")||!strcmp(fault,"maps")||!strcmp(fault,"library"))) {
            const char *suffix=!strcmp(fault,"proof")?"/proof":!strcmp(fault,"maps")?"/proc/123/maps":"/opt/eye.so";
            if(snprintf(target,sizeof target,"%s%s",root,suffix)<(int)sizeof target) (void)unlink(target);
        }
    }
#endif
    return unchanged(&ctx) && eye_publish_token(root,proof.pid,proof.starttime,proof.generation,unchanged,&ctx);
}
int main(int argc,char **argv) {
    char *end; long pid; int ok;
    if(argc!=4) { puts("NATIVE"); return 0; }
    errno=0; pid=strtol(argv[3],&end,10); if(errno||end==argv[3]||*end||pid<0) { puts("NATIVE"); return 0; }
    ok=run(argv[1],argv[2],pid);
    puts(!ok?"NATIVE":!strcmp(argv[1],"resume")?"ENABLE":"CANDIDATE"); return 0;
}
#else
int main(void) { puts("NATIVE"); return 0; }
#endif
