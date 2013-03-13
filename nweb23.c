#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "nweb23.h"

/* This is a child web server thread */
void http_server(struct request *__r)
{
  struct http_request *r = (struct http_request *)__r;
  int j, file_fd, buflen;
  long i, ret, len;
  char * fstr;
  char buffer[BUFSIZE+1]; 

  /* Read Web request in one go */
  ret =read(r->socketfd, buffer, BUFSIZE);   
  if(ret == 0 || ret == -1) {  /* read failure stop now */
    logger(FORBIDDEN, "failed to read browser request", "", r->socketfd);
  }

  if(ret > 0 && ret < BUFSIZE)  /* return code is valid chars */
    buffer[ret]=0;    /* terminate the buffer */
  else
      buffer[0]=0;

  for(i=0; i<ret; i++)  /* remove CF and LF characters */
    if(buffer[i] == '\r' || buffer[i] == '\n')
      buffer[i]='*';

  logger(LOG, "request", buffer, r->req.id);
  if(strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4) ) {
    logger(FORBIDDEN,"Only simple GET operation supported", buffer, r->socketfd);
  }

  for(i=4; i<BUFSIZE; i++) { /* null terminate after the second space to ignore extra stuff */
    if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
      buffer[i] = 0;
      break;
    }
  }

   /* check for illegal parent directory use .. */
  for(j=0; j<i-1; j++) {
    if(buffer[j] == '.' && buffer[j+1] == '.') {
      logger(FORBIDDEN,"Parent directory (..) path names not supported", buffer, r->socketfd);
    }
  }

  /* convert no filename to index file */
  if(!strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6))
    (void)strcpy(buffer,"GET /index.html");

  /* work out the file type and check we support it */
  buflen=strlen(buffer);
  fstr = (char *)0;
  for(i=0; extensions[i].ext != 0; i++) {
    len = strlen(extensions[i].ext);
    if(!strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
      fstr =extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0) {
    logger(FORBIDDEN,"file extension type not supported", buffer, r->socketfd);
  }

  /* open the file for reading */
  if((file_fd = open(&buffer[5], O_RDONLY)) == -1) {
    logger(NOTFOUND, "failed to open file", &buffer[5], r->socketfd);
  }

  logger(LOG, "SEND", &buffer[5], r->req.id);
   /* lseek to the file end to find the length */
  len = lseek(file_fd, (off_t)0, SEEK_END);
   /* lseek back to the file start ready for reading */
  lseek(file_fd, (off_t)0, SEEK_SET);

    sprintf(buffer, page_data[OK_HEADER], VERSION, len, fstr); /* Header + a blank line */
  logger(LOG, "Header", buffer, r->req.id);
  write(r->socketfd, buffer, strlen(buffer));

  /* send file in 8KB block - last block may be smaller */
  while((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
    write(r->socketfd, buffer, ret);
  }
  /* Allow socket to drain before signalling the socket is closed */
  shutdown(r->socketfd, SHUT_WR);
  for(;;) {
    int res = read(r->socketfd, buffer, 4000);
        if(res < 0)
      logger(ERROR, "Connection reset by peer.", buffer, r->req.id);
        if(!res)
      break;
  }
  close(r->socketfd);
}

int main(int argc, char **argv)
{
  int i, port, pid, listenfd, socketfd;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */

  if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
    (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
  "\tnweb is a small and very safe mini web server\n"
  "\tnweb only servers out file/web pages with extensions named below\n"
  "\t and only from the named directory or its sub-directories.\n"
  "\tThere is no fancy features = safe and secure.\n\n"
  "\tExample: nweb 8181 /home/nwebdir &\n\n"
  "\tOnly Supports:", VERSION);
    for(i=0;extensions[i].ext != 0;i++)
      (void)printf(" %s",extensions[i].ext);

    (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
  "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
  "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
    exit(0);
  }
  if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
      !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
      !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
      !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
    printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
    exit(3);
  }
  if(chdir(argv[2]) == -1){ 
    printf("ERROR: Can't Change to directory %s\n",argv[2]);
    exit(4);
  }
  fflush(stdout);
  logger(LOG, "nweb starting", argv[1], getpid());

  /* setup the network socket */
  if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    logger(ERROR, "system call", "socket", 0);

  port = atoi(argv[1]);
  if(port < 0 || port >60000)
    logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if(bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    logger(ERROR, "system call", "bind", 0);
  if(listen(listenfd, 64) < 0)
    logger(ERROR, "system call", "listen", 0);

  struct request_queue q;
  request_queue_init(&q, http_server, sizeof(struct http_request));
  tpool_init(&q, 2*get_nprocs());
  for(;;) {
    length = sizeof(cli_addr);
    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "system call", "accept", 0);
    }
    else {
      struct http_request *r;
      r = create_request(&q);
      r->socketfd = socketfd;
      enqueue_request(&q, &r->req);
    }
  }
}
