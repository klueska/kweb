#ifndef KWEB_H
#define KWEB_H

#define DEBUG
#include "tpool.h"

#define VERSION  "1.0"

#define ERROR       42
#define LOG         44
#define BUFSIZE   8096

#define FORBIDDEN  403
#define NOTFOUND   404

struct http_request {
  struct request req;
  int socketfd;
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
  "HTTP/1.1 403 Forbidden\n"
  "Content-Length: 185\n"
  "Connection: close\n"
  "Content-Type: text/html\n\n"
  "<html><head>\n"
  "<title>403 Forbidden</title>\n"
  "</head><body>\n"
  "<h1>Forbidden</h1>\n"
  "The requested URL, file type or operation is not allowed on this simple static file webserver.\n"
  "</body></html>\n",

  "HTTP/1.1 404 Not Found\n"
  "Content-Length: 136\n"
  "Connection: close\n"
  "Content-Type: text/html\n\n"
  "<html><head>\n"
  "<title>404 Not Found</title>\n"
  "</head><body>\n"
  "<h1>Not Found</h1>\n"
  "The requested URL was not found on this server.\n"
  "</body></html>\n",

  "HTTP/1.1 200 OK\n"
  "Server: kweb/%s\n"
  "Content-Length: %ld\n"
  "Connection: close\n"
  "Content-Type: %s\n\n"
};

#ifndef DEBUG
#define logger(type, s1, s2, socket_fd) ({type;})
#else
void logger(int type, char *s1, char *s2, int socket_fd)
{
  int fd;
  char logbuffer[BUFSIZE*2];

  switch (type) {
    case ERROR:
      sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",
              s1, s2, errno, getpid()); 
      break;
    case FORBIDDEN: 
      sprintf(logbuffer,"FORBIDDEN: %s:%s", s1, s2); 
      break;
    case NOTFOUND: 
      sprintf(logbuffer,"NOT FOUND: %s:%s", s1, s2); 
      break;
    case LOG:
      sprintf(logbuffer," INFO: %s:%s:%d", s1, s2, socket_fd);
      break;
  }  
  /* No checks here, nothing can be done with a failure anyway */
  if((fd = open("kweb.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
    write(fd, logbuffer, strlen(logbuffer)); 
    write(fd, "\n", 1);      
    close(fd);
  }
}
#endif // DEBUG
#endif // KWEB_H
