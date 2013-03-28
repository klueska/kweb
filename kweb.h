#ifndef KWEB_H
#define KWEB_H

#include <sys/epoll.h>
#include "pthread.h"
#include "kqueue.h"

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
  pthread_mutex_t writelock;
  int epollrfd;
  int epollwfd;
};

struct {
  char *ext;
  char *filetype;
} extensions [] = {
  {"gif", "image/gif" },  
  {"jpg", "image/jpg" }, 
  {"jpeg","image/jpeg"},
  {"png", "image/png" },  
  {"ico", "image/ico" },  
  {"zip", "image/zip" },  
  {"gz",  "image/gz"  },  
  {"tar", "image/tar" },  
  {"htm", "text/html" },  
  {"html","text/html" },  
  {0,0}
};

enum {
  FORBIDDEN_PAGE,
  NOTFOUND_PAGE,
  OK_HEADER,
};
char *page_data[] = {
  "HTTP/1.1 403 Forbidden\r\n"
  "Content-Length: 185\r\n"
  "Content-Type: text/html\r\n\r\n"
  "<html><head>\n"
  "<title>403 Forbidden</title>\n"
  "</head><body>\n"
  "<h1>Forbidden</h1>\n"
  "The requested URL, file type or operation is not allowed on this simple static file webserver.\n"
  "</body></html>\n",

  "HTTP/1.1 404 Not Found\r\n"
  "Content-Length: 138\r\n"
  "Content-Type: text/html\r\n\r\n"
  "<html><head>\n"
  "<title>404 Not Found</title>\n"
  "</head><body>\n"
  "<h1>Not Found</h1>\n"
  "The requested URL was not found on this server.\n"
  "</body></html>\n",

  "HTTP/1.1 200 OK\r\n"
  "Server: kweb/%s\r\n"
  "Content-Length: %ld\r\n"
  "Content-Type: %s\r\n\r\n"
};

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
