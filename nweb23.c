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

/* This is a wrapper to allow us to drain a socket before closing it */
static inline void finish_request(struct http_request *r)
{
  /* Allow socket to drain before closing it */
  char buffer[4096];
  for(;;) {
    int res = read(r->socketfd, buffer, 4096);
    if(res < 0)
      logger(LOG, "Connection reset by peer.", buffer, r->req.id);
    if(!res)
      break;
  }
  close(r->socketfd);
}

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
    logger(FORBIDDEN, "Failed to read browser request", "", r->socketfd);
    write(r->socketfd, page_data[FORBIDDEN_PAGE], 271);
    finish_request(r);
    return;
  }

  /* Check if return code is valid number of chars
     and terminate the buffer appropriately */
  if(ret > 0 && ret < BUFSIZE)
    buffer[ret]=0;
  else
    buffer[0]=0;

  /* Remove CF and LF characters */
  for(i=0; i<ret; i++)
    if(buffer[i] == '\r' || buffer[i] == '\n')
      buffer[i]='*';

  /* Make sure it's a GET operation */
  logger(LOG, "Request", buffer, r->req.id);
  if(strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
    logger(FORBIDDEN, "Only simple GET operation supported", buffer, r->socketfd);
    write(r->socketfd, page_data[FORBIDDEN_PAGE], 271);
    finish_request(r);
    return;
  }

  /* Null terminate after the second space to ignore extra stuff */
  for(i=4; i<BUFSIZE; i++) {
    /* String is "GET URL " + lots of other stuff */
    if(buffer[i] == ' ') {
      buffer[i] = 0;
      break;
    }
  }

  /* Check for illegal parent directory use .. */
  for(j=0; j<i-1; j++) {
    if(buffer[j] == '.' && buffer[j+1] == '.') {
      logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, r->socketfd);
      write(r->socketfd, page_data[FORBIDDEN_PAGE], 271);
      finish_request(r);
      return;
    }
  }

  /* Convert no filename to index file */
  if(!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6))
    strcpy(buffer, "GET /index.html");

  /* Work out the file type and check we support it */
  buflen=strlen(buffer);
  fstr = 0;
  for(i=0; extensions[i].ext != 0; i++) {
    len = strlen(extensions[i].ext);
    if(!strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
      fstr =extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0) {
    logger(FORBIDDEN, "File extension type not supported", buffer, r->socketfd);
    write(r->socketfd, page_data[FORBIDDEN_PAGE], 271);
    finish_request(r);
    return;
  }

  /* Open the file for reading */
  if((file_fd = open(&buffer[5], O_RDONLY)) == -1) {
    logger(NOTFOUND, "Failed to open file", &buffer[5], r->socketfd);
    write(r->socketfd, page_data[NOTFOUND_PAGE], 224);
    finish_request(r);
    return;
  }

  /* Get the File length */
  len = lseek(file_fd, 0, SEEK_END);
  lseek(file_fd, 0, SEEK_SET);

  /* Start sending a response */
  logger(LOG, "SEND", &buffer[5], r->req.id);

  /* Send the necessary header info + a blank line */
  sprintf(buffer, page_data[OK_HEADER], VERSION, len, fstr);
  logger(LOG, "Header", buffer, r->req.id);
  write(r->socketfd, buffer, strlen(buffer));

  /* Send the file itself in 8KB chunks - last block may be smaller */
  while((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
    write(r->socketfd, buffer, ret);
  }
  finish_request(r);
}

int main(int argc, char **argv)
{
  int port, pid, listenfd, socketfd;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */

  /* Verify proper number of args and print usage if invalid */
  if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
    printf(""
      "nweb - Version %d\n"
      "Usage: nweb <port_number> <top_directory>\n"
      "Example: nweb 8181 /home/nwebdir &\n\n"
      "\tnweb is a small and very safe mini web server\n"
      "\tnweb only servers out file/web pages with extensions named below\n"
      "\t and only from the named directory or its sub-directories.\n"
      "\tThere are no fancy features = safe and secure.\n\n"
      "\tOnly Supports:", VERSION);
    for(int i=0; extensions[i].ext != 0; i++)
      printf(" %s", extensions[i].ext);
    printf("\n\n"
      "\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
      "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin\n"
      "\tNo warranty given or implied\n"
      "\tKevin Klues <klueska@cs.berkeley.edu>\n"
      "\t(Adapted from nweb by Nigel Griffiths <nag@uk.ibm.com>\n");
    exit(0);
  }

  /* Make sure the specified ROOT directory is a valid one */
  if(!strncmp(argv[2], "/"   , 2 ) || !strncmp(argv[2], "/etc", 5 ) ||
     !strncmp(argv[2], "/bin", 5 ) || !strncmp(argv[2], "/lib", 5 ) ||
     !strncmp(argv[2], "/tmp", 5 ) || !strncmp(argv[2], "/usr", 5 ) ||
     !strncmp(argv[2], "/dev", 5 ) || !strncmp(argv[2], "/sbin", 6)){
    printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
    exit(1);
  }

  /* Verify that the specified port number is a valid one */
  port = atoi(argv[1]);
  if(port < 0 || port >60000) {
    printf("ERROR: Invalid port number %d (try 1->60000)", port);
    exit(1);
  }

  /* Change to the specified ROOT directory to set it as the ROOT of the fs */
  if(chdir(argv[2]) == -1){ 
    printf("ERROR: Can't Change to directory %s\n", argv[2]);
    exit(1);
  }

  /* Setup the network socket */
  if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("ERROR: System call - socket");
    exit(1);
  }

  /* Bind to the specified address and listen on the specified port */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if(bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("ERROR: System call - bind");
    exit(1);
  }
  if(listen(listenfd, 64) < 0) {
    printf("ERROR: System call - listen");
    exit(1);
  }

  /* Start accepting requests and processing them */
  fflush(stdout);
  logger(LOG, "Nweb starting", argv[1], getpid());

  struct request_queue q;
  request_queue_init(&q, http_server, sizeof(struct http_request));
  tpool_init(&q, 2*get_nprocs());
  for(;;) {
    length = sizeof(cli_addr);
    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "System call", "accept", 0);
    }
    else {
      struct http_request *r;
      r = create_request(&q);
      r->socketfd = socketfd;
      enqueue_request(&q, &r->req);
    }
  }
}
