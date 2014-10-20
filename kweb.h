#ifndef KWEB_H
#define KWEB_H

#include <pthread.h>
#include "kqueue.h"
#include "os.h"

#define VERSION  "1.0"

#define ERROR       42
#define LOG         44
#define BUFSIZE   8192

#define FORBIDDEN  403
#define NOTFOUND   404

#define MAX_BURST   10
#define KWEB_SREAD_TIMEOUT  (1000*30)
#define KWEB_SWRITE_TIMEOUT      (-1)

enum {
  REQ_NEW,
  REQ_ALIVE
};

struct server_stats {
  uint64_t tsc_freq;
};

struct http_request {
  char rsp_header[100];
  char buf[BUFSIZE+1];
  int length;
};

struct http_connection {
  struct kitem conn;
  unsigned int burst_length;
  int ref_count;
  int socketfd;
  int buf_length;
  char buf[BUFSIZE+1];
  mutex_t writelock;
  /* TODO: these are linux specific, consider hiding them better */
  int epollrfd;
  int epollwfd;
};

enum {
  FORBIDDEN_PAGE,
  NOTFOUND_PAGE,
  OK_HEADER,
  URLCMD_PAGE,
};

extern struct tpool tpool;

void enqueue_connection_tail(struct kqueue *q, struct http_connection *c);
void enqueue_connection_head(struct kqueue *q, struct http_connection *c);

void http_server(struct kqueue *q, struct kitem *__c);

/* OS dependent, in linux.c or akaros.c */
void os_init(void);
void init_connection(struct http_connection *c);
void destroy_connection(struct http_connection *c);
ssize_t timed_read(struct http_connection *c, void *buf, size_t count);
ssize_t timed_write(struct http_connection *c, const void *buf, size_t count);
void dispatch_call(int call_fd, void *client_addr);

#ifndef DEBUG
#define logger(type, s1, s2, socket_fd) ({type;})
#else
void logger(int type, char *s1, char *s2, int socket_fd)
{
  int fd;
  char *logbuffer = malloc(BUFSIZE*2);

  switch (type) {
    case ERROR:
      sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d",
              s1, s2, errno, getpid()); 
      break;
    case FORBIDDEN: 
      sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2); 
      break;
    case NOTFOUND: 
      sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2); 
      break;
    case LOG:
      sprintf(logbuffer, "INFO: %s:%s:%d", s1, s2, socket_fd);
      break;
  }  
  /* No checks here, nothing can be done with a failure anyway */
  if((fd = open("kweb.log", O_CREAT | O_WRONLY | O_APPEND, 0643)) >= 0) {
    write(fd, logbuffer, strlen(logbuffer)); 
    write(fd, "\n", 1);      
    close(fd);
  }
  free(logbuffer);
}
#endif // DEBUG
#endif // KWEB_H
