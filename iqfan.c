/* iqfan — rtl_sdr -> multi-client rtl_tcp-compatible raw-IQ fan-out (C).
 * usage: iqfan --port <port> [rtl_sdr options...]   (rtl_sdr always used; "-" appended)
 * --port (not -p) so rtl_sdr's -p (ppm) passes through untouched.
 * Outbound = raw IQ (rtl_tcp data format): csdr/raw-IQ apps and WebSDR (rtl_tcp) read
 * the same stream. Inbound client bytes (rtl_tcp commands) drained+ignored (fixed cfg).
 * Slow clients dropped (bounded buffer). Child EOF/death -> close clients (clean
 * disconnect) + respawn. SIGINT/SIGTERM -> kill child, close clients, exit cleanly.
 * If rtl_sdr is not installed, reports that it is required and exits.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/stat.h>

#define BUF  (1u<<20)
#define MAXC 64
#define CHUNK 65536

/* rtl_tcp greeting: "RTL0" + tuner(5) + gain_count(0), BIG-endian u32 —
 * matches the proven sdroxide_proxy.py header (struct.pack("!II",5,0)). */
static const unsigned char hdr[12]={'R','T','L','0', 0,0,0,5, 0,0,0,0};
typedef struct { int fd, dead; unsigned char *b; size_t head, tail; } Client;
static Client *cl[MAXC]; static int nc;
static char **rargv; static pid_t child; static int cfd;
static volatile sig_atomic_t stop;

static void onsig(int s){ (void)s; stop=1; }
static int known_rtl(const char*s){
    static const char*k[]={"-f","-s","-g","-d","-p","-b","-n","-S","-D","-T","-A","-E","-R","-I","-Q","-P"};
    for(unsigned i=0;i<sizeof k/sizeof*k;i++) if(!strcmp(s,k[i])) return 1;
    return 0;
}
static void usage(FILE*out){
    fprintf(out,
"Usage: iqfan --port <port> [--daemon] [--log <file>] [rtl_sdr options...]\n"
"  --port <p>, --port=<p>  TCP listen port (required)\n"
"  --daemon                run in background (fork+setsid)\n"
"  --log <file>            log stdout/stderr (default /dev/null)\n"
"  --help, -h              show this help\n"
"Always runs rtl_sdr (appends '-'); serves raw IQ as rtl_tcp (RTL0 header).\n"
"Pass-through rtl_sdr opts: -f -s -g -d -p -b -n -S -D -T -A -E -R -I -Q -P\n");
}
static void nb(int fd){ fcntl(fd, F_SETFL, fcntl(fd,F_GETFL,0)|O_NONBLOCK); }
static void killc(Client*c){ c->dead=1; }
static void sweep(void){
    for(int i=0;i<nc;i++) if(cl[i]->dead){
        close(cl[i]->fd); free(cl[i]->b); free(cl[i]);
        cl[i]=cl[--nc]; i--;
    }
}
static void closeall(void){ for(int i=0;i<nc;i++) cl[i]->dead=1; sweep(); }
static pid_t spawn(void){
    int p[2]; if(pipe(p)) exit(1);
    pid_t pid=fork();
    if(pid==0){
        setpgid(0,0);                 /* own pgroup: ctrl+C hits iqfan, we kill child */
        close(p[0]); dup2(p[1],1); close(p[1]);
        execvp(rargv[0], rargv);
        _exit(127);                   /* rtl_sdr missing */
    }
    close(p[1]); nb(p[0]); cfd=p[0]; return pid;
}
static void cleanup(void){
    closeall();
    if(child>0){
        kill(child,SIGTERM);                      /* graceful first */
        for(int i=0;i<10;i++){ int st; if(waitpid(child,&st,WNOHANG)==child){ child=0; break; } usleep(100000); }
        if(child>0){ kill(child,SIGKILL); waitpid(child,0,0); child=0; }  /* force if stuck */
    }
}

int main(int argc,char**argv){
    /* port = --port <v> / --port=<v>; -p is left for rtl_sdr (ppm); append "-" */
    int port=0, daemon=0; const char *logfile=NULL;
    rargv=malloc((argc+3)*sizeof*rargv);
    int n=0; rargv[n++]="rtl_sdr";
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--port") && i+1<argc){ port=atoi(argv[++i]); continue; }
        if(!strncmp(argv[i],"--port=",7)){ port=atoi(argv[i]+7); continue; }
        if(!strcmp(argv[i],"--daemon")){ daemon=1; continue; }
        if(!strcmp(argv[i],"--log") && i+1<argc){ logfile=argv[++i]; continue; }
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){ usage(stdout); return 0; }
        if(argv[i][0]=='-' && !known_rtl(argv[i])){
            fprintf(stderr,"iqfan: unknown option '%s' (see --help)\n",argv[i]); return 1;
        }
        rargv[n++]=argv[i];
    }
    if(!port){ fprintf(stderr,"iqfan: missing --port\n"); usage(stderr); return 1; }
    if(daemon){
        pid_t p=fork(); if(p<0){ perror("fork"); return 1; } if(p>0) _exit(0);
        setsid();
        int nul=open("/dev/null",O_RDWR); if(nul>=0){ dup2(nul,0); if(nul>2)close(nul); }
        int fd=open(logfile?logfile:"/dev/null",O_WRONLY|O_CREAT|O_APPEND,0644);
        if(fd>=0){ dup2(fd,1); dup2(fd,2); if(fd>2)close(fd); }
    }
    rargv[n++]="-"; rargv[n]=NULL;

    signal(SIGPIPE,SIG_IGN);
    signal(SIGINT,onsig); signal(SIGTERM,onsig);
    int lfd=socket(AF_INET,SOCK_STREAM,0), one=1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    /* wait for a previous instance to die (port free) so no manual sleep needed */
    int tries=0;
    while(bind(lfd,(void*)&a,sizeof a)<0){
        if(errno==EADDRINUSE && tries<50){ usleep(200000); tries++; continue; }
        perror("bind"); return 1;
    }
    if(listen(lfd,16)){ perror("listen"); return 1; }
    nb(lfd);
    child=spawn();
    unsigned char buf[CHUNK];
    for(;;){
        if(stop) break;
        struct pollfd pf[MAXC+2]; int map[MAXC+2]; int n2=0, respawn=0, fatal=0;
        pf[n2].fd=lfd; pf[n2].events=POLLIN; map[n2]=-1; n2++;
        pf[n2].fd=cfd; pf[n2].events=POLLIN; map[n2]=-2; n2++;
        /* POLLOUT only when pending data, else poll spins on always-writable sockets */
        for(int i=0;i<nc;i++){ pf[n2].fd=cl[i]->fd; pf[n2].events=POLLIN|((cl[i]->head>cl[i]->tail)?POLLOUT:0); map[n2]=i; n2++; }
        int r=poll(pf,n2,200);
        int st; if(waitpid(child,&st,WNOHANG)==child){
            child=0;
            if(WIFEXITED(st)&&WEXITSTATUS(st)==127) fatal=1; else respawn=1;
        }
        if(fatal){ fprintf(stderr,"iqfan: rtl_sdr not found — it is required (install rtl-sdr)\n"); closeall(); return 1; }
        if(stop) break;
        if(r<0){ if(errno==EINTR) goto end; perror("poll"); break; }
        for(int k=0;k<n2;k++){
            if(!pf[k].revents) continue;
            int mi=map[k];
            if(mi==-1){
                int c=accept(lfd,0,0);
                if(c<0) continue;
                if(nc>=MAXC){ close(c); continue; }
                nb(c); setsockopt(c,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);
                /* RST on close: WebSDR rtl_tcp client detects reset, not clean FIN */
                struct linger lg={1,0}; setsockopt(c,SOL_SOCKET,SO_LINGER,&lg,sizeof lg);
                Client*c2=calloc(1,sizeof*c2); c2->fd=c; c2->b=malloc(BUF);
                memcpy(c2->b,hdr,sizeof hdr); c2->head=sizeof hdr;  /* greet first */
                cl[nc++]=c2;
            } else if(mi==-2){
                ssize_t got=read(cfd,buf,sizeof buf);
                if(got>0){
                    for(int i=0;i<nc;i++){
                        Client*c=cl[i]; size_t used=c->head-c->tail;
                        if(BUF-used<(size_t)got){ killc(c); continue; }
                        if(c->head+got>BUF){ memmove(c->b,c->b+c->tail,used); c->head=used; c->tail=0; }
                        memcpy(c->b+c->head,buf,got); c->head+=got;
                    }
                } else if(got==0||(got<0&&errno!=EAGAIN&&errno!=EINTR)) respawn=1;
            } else {
                Client*c=cl[mi];
                if(pf[k].revents&POLLIN){ unsigned char d[4096]; while(read(c->fd,d,sizeof d)>0){} }
                if(pf[k].revents&(POLLHUP|POLLERR)){ killc(c); continue; }
                if(pf[k].revents&POLLOUT){
                    size_t used=c->head-c->tail;
                    if(used){
                        ssize_t put=write(c->fd,c->b+c->tail,used);
                        if(put>0){ c->tail+=put; if(c->tail==c->head) c->tail=c->head=0; }
                        else if(put<0&&errno!=EAGAIN&&errno!=EINTR) killc(c);
                    }
                }
            }
        }
end:
        sweep();
        if(stop) break;
        if(respawn||child==0){ closeall(); close(cfd); usleep(200000); child=spawn(); }
    }
    cleanup();
    close(lfd);
    return 0;
}
