#include <stdio.h>
#include <limits.h>
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
#include <signal.h>

//#define DEBUG
#include "kweb.h"
#include "cpu_util.h"
#include "ktimer.h"

static int listenfd;
static struct ktimer ktimer;
static struct request_queue request_queue;
static struct tpool tpool;
static struct cpu_util cpu_util;

static struct request_queue_stats rqstats_prev = {0};
static struct request_queue_stats rqstats_curr = {0};
static struct tpool_stats tpstats_prev = {0};
static struct tpool_stats tpstats_curr = {0};
static struct cpu_util_stats custats_prev = {0};
static struct cpu_util_stats custats_curr = {0};

static void sig_int(int signo);
static void sig_pipe(int signo);
static void ktimer_callback(void *arg);
static void print_interval_statistics();
static void print_lifetime_statistics();

static int find_crlf(char *buf, int max_len)
{
  int loc = -1;
  int cri = 0;
  for(int i=0; i<max_len; i++) {
    if(buf[i] == '\r') {
      cri = i;
    }
    if((cri == (i-1)) && (buf[i] == '\n')) {
      loc = cri;
      break;
    }
  }
  return loc;
}

static int find_request(char *src, int max_len)
{
  int i = 0;
  char *curr_line = NULL;
  int curr_line_len = 0;
  int content_length = 0;
  int request_len = 0;
  while(i < max_len) {
    curr_line = &src[i];
    curr_line_len = find_crlf(curr_line, max_len-i);
    if(curr_line_len < 0) {
      i = -1; break;
    }
    if(curr_line_len == 0) {
      i += 2; break;
    }
    if(curr_line_len > 16 && (!strncmp(curr_line, "Content-Length: ", 16))) {
      int cls_len = curr_line_len-16;
      char *cls = alloca(cls_len+1);
      cls[cls_len] = '\0';
      memcpy(cls, &curr_line[16], cls_len);
      content_length = atoi(cls);
    }
    i += curr_line_len+2;
  }
  if(i <= 0 || (i+content_length) > max_len)
    return -1;
  request_len = i + content_length;
  return request_len;
}

static int buffer_next_or_die(struct http_request *r)
{
  if(r->ibuf_length)
    memmove(r->ibuf, &r->ibuf[r->req_length], r->ibuf_length);

  int ret = 0;
  while(1) {
    /* Find a request in the ibuf */
    int len = find_request(r->ibuf, r->ibuf_length);
    
    /* If we found one, update some variables and return to process it */
    if(len > 0) {
      r->req_length = len;
      r->ibuf_length = r->ibuf_length - len;
      return len;
    }

    /* Otherwise, try and read in the next request from the socket */
    ret = read(r->socketfd, &r->ibuf[r->ibuf_length],
               sizeof(r->ibuf) - r->ibuf_length);   

    /* If the return code is invalid or marks the end of a connection, break
     * and deal with it. */
    if(ret <= 0)
      break;

    /* Otherwise, update the ibuf_length and loop back around to try and
     * extract the request again. */
    r->ibuf_length += ret;
  }

  /* Otherwise... */
  switch(r->state) {
    case REQ_NEW:
      /* Fail on a new request, and send back a FORBIDDEN error */
      logger(FORBIDDEN, "Failed to read browser request", "", r->socketfd);
      write(r->socketfd, page_data[FORBIDDEN_PAGE], 271);
      break;
    case REQ_ALIVE:
	  /* On a reenqueued request, it is OK for ret to return either no bytes or
	   * an error code. If it's an error code, simply log that the connection
	   * was killed prematurely by the client.*/
      if(ret == -1)
        logger(LOG, "Connection reset by peer.", "", r->req.id);
      break;
  }
  /* In any case, close the socket because we are done with it */
  close(r->socketfd);
  return ret;
}

static void reenqueue_or_complete(struct request_queue *q,
                                  struct http_request *r)
{
  if(buffer_next_or_die(r) > 0)
    request_queue_enqueue_request(q, &r->req);
  else
    request_queue_destroy_request(q, &r->req);
}

static int intercept_url(char *url)
{
  if(!strncmp(url, "/start_timer", 12)) {
    ktimer_start(&ktimer);
    return 1;
  }
  if(!strncmp(url, "/stop_timer", 11)) {
    ktimer_stop(&ktimer);
    return 1;
  }
  return 0;
}

/* This is a child web server thread */
void http_server(struct request_queue *q, struct request *__r)
{
  struct http_request *r = (struct http_request *)__r;
  int j, file_fd, buflen;
  long i = 0, ret = 0, len = 0;
  char *fstr;
  char *request_line;
  char *saveptr;

  /* If this is a new request, buffer it up,
   * or destroy it and return if that is unsuccessful. */
  if(r->state == REQ_NEW) {
    if(buffer_next_or_die(r) <= 0) {
      request_queue_destroy_request(q, &r->req);
      return;
    }
  }
  r->state = REQ_ALIVE;

  /* Otherwise ...
   * Parse through the request, grabbing only what we care about */
  request_line = strtok_r(r->ibuf, "\r\n", &saveptr);

  /* Make sure it's a GET operation */
  if(strncmp(request_line, "GET ", 4) && strncmp(request_line, "get ", 4)) {
    logger(FORBIDDEN, "Only simple GET operation supported", request_line, r->socketfd);
    write(r->socketfd, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
    close(r->socketfd);
    return;
  }
  logger(LOG, "Request", request_line, r->req.id);

  /* Intercept certain urls and do something special. */
  if(intercept_url(&request_line[4])) {
    /* Send the necessary header info + a blank line */
    sprintf(r->obuf, page_data[OK_HEADER], VERSION, 0, "text/plain");
    logger(LOG, "INTERCEPT URL", url, r->req.id);
    write(r->socketfd, r->obuf, strlen(r->obuf));

    reenqueue_or_complete(q, r);
    return;
  }

  /* Strip all query data and the version info from the request_line */
  for(i=4; i<strlen(request_line); i++) {
    /* String is "GET URL?<query_data> HTTP_VERSION" */
    if(request_line[i] == ' ' || request_line[i] == '?') {
      request_line[i] = '\0';
      break;
    }
  }

  /* Otherwise, check for illegal parent directory use .. */
  for(j=4; j<i-1; j++) {
    if(request_line[j] == '.' && request_line[j+1] == '.') {
      logger(FORBIDDEN, "Parent directory (..) path names not supported", request_line, r->socketfd);
      write(r->socketfd, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
      close(r->socketfd);
      return;
    }
  }

  /* Convert no filename to index file */
  if(!strncmp(request_line, "GET /\0", 6) || !strncmp(request_line, "get /\0", 6))
    strcpy(request_line, "GET /index.html");

  /* Work out the file type and check we support it */
  buflen=strlen(request_line);
  fstr = 0;
  for(i=0; extensions[i].ext != 0; i++) {
    len = strlen(extensions[i].ext);
    if(!strncmp(&request_line[buflen-len], extensions[i].ext, len)) {
      fstr =extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0) {
    logger(FORBIDDEN, "File extension type not supported", request_line, r->socketfd);
    write(r->socketfd, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
    close(r->socketfd);
    return;
  }

  /* Open the file for reading */
  if((file_fd = open(&request_line[5], O_RDONLY)) == -1) {
    logger(NOTFOUND, "Failed to open file", &request_line[5], r->socketfd);
    write(r->socketfd, page_data[NOTFOUND_PAGE], strlen(page_data[NOTFOUND_PAGE]));
    close(r->socketfd);
    return;
  }

  /* Get the File length */
  len = lseek(file_fd, 0, SEEK_END);
  lseek(file_fd, 0, SEEK_SET);

  /* Start sending a response */
  logger(LOG, "SEND", &request_line[5], r->req.id);

  /* Send the necessary header info + a blank line */
  sprintf(r->obuf, page_data[OK_HEADER], VERSION, len, fstr);
  logger(LOG, "Header", r->obuf, r->req.id);
  write(r->socketfd, r->obuf, strlen(r->obuf));

  /* Send the file itself in 8KB chunks - last block may be smaller */
  while((ret = read(file_fd, r->obuf, sizeof(r->obuf))) > 0) {
    if(write(r->socketfd, r->obuf, ret) < 0) {
      logger(LOG, "Write error on socket.", "", r->socketfd);
      close(file_fd);
      close(r->socketfd);
      return;
    }
  }
  close(file_fd);
  reenqueue_or_complete(q, r);
}

int main(int argc, char **argv)
{
  int tpool_size = INT_MAX;
  int port, pid, socketfd;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */

  /* Verify proper number of args and print usage if invalid */
  if( argc < 3  || argc > 4 || !strcmp(argv[1], "-?") ) {
    printf(""
    "kweb - Version %s\n"
    "Usage: kweb <port_number> <top_directory> [<tpool_size=MAX_INT>]\n"
    "Example: kweb 8181 /home/kwebdir &\n\n"
    "         kweb 8181 /home/kwebdir 50 &\n\n"

    "kweb is a small and safe multi-threaded static web server\n"
    "It only serves files with the extensions named below.\n"
    "It also only serves files from the named directory or its sub-directories.\n\n"

    "Supports:", VERSION);
    for(int i=0; extensions[i].ext != 0; i++)
      printf(" %s", extensions[i].ext);
    printf(""
    "\n"
    "Not Supported: URLs including \"..\", Java, Javascript, CGI\n"
    "Not Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin\n\n"

    "No warranty given or implied\n"
    "Kevin Klues <klueska@cs.berkeley.edu>\n"
    "(Adapted from nweb Version 23 by Nigel Griffiths <nag@uk.ibm.com>)\n");
    exit(0);
  }

  /* Make sure the specified ROOT directory is a valid one */
  if(!strncmp(argv[2], "/"   , 2 ) || !strncmp(argv[2], "/etc", 5 ) ||
     !strncmp(argv[2], "/bin", 5 ) || !strncmp(argv[2], "/lib", 5 ) ||
     !strncmp(argv[2], "/tmp", 5 ) || !strncmp(argv[2], "/usr", 5 ) ||
     !strncmp(argv[2], "/dev", 5 ) || !strncmp(argv[2], "/sbin", 6)){
    printf("ERROR: Bad top directory %s, see kweb -?\n", argv[2]);
    exit(1);
  }

  /* Verify that the specified port number is a valid one */
  port = atoi(argv[1]);
  if(port < 0 || port > 60000) {
    printf("ERROR: Invalid port number %d (try 1->60000)\n", port);
    exit(1);
  }

  /* Change to the specified ROOT directory to set it as the ROOT of the fs */
  if(chdir(argv[2]) == -1){ 
    printf("ERROR: Can't Change to directory %s\n", argv[2]);
    exit(1);
  }

  /* Change to the specified ROOT directory to set it as the ROOT of the fs */
  if(argc == 4) {
    tpool_size = atoi(argv[3]);
    if(tpool_size < 0 || tpool_size > INT_MAX) { 
      printf("ERROR: Invalid tpool size %d\n", tpool_size);
      exit(1);
    }
  }

  /* Setup the network socket */
  if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("ERROR: System call - socket\n");
    exit(1);
  }

  /* Set the sockopts so that we can rebind in case of ungracefully shutting
   * down the server */
  int yes = 1;
  if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) {
    printf("ERROR: System call - setsockopt\n");
    exit(1);
  }

  /* Bind to the specified address and listen on the specified port */
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);
  if(bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("ERROR: System call - bind\n");
    exit(1);
  }
  if(listen(listenfd, 64) < 0) {
    printf("ERROR: System call - listen\n");
    exit(1);
  }

  /* Register a signal handler so we can print out some statistics once we kill
   * the server */
  if(signal(SIGINT, sig_int) == SIG_ERR) {
    printf("\nCan't catch SIGINT\n");
    exit(1);
  }

  /* Register a signal handler for sigpipe in case someone closes a socket
   * while we are in the middle of writing it */
  if(signal(SIGPIPE, sig_pipe) == SIG_ERR) {
    printf("\nCan't catch SIGPIPE\n");
    exit(1);
  }

  /* Start accepting requests and processing them */
  fflush(stdout);
  logger(LOG, "Starting kweb", argv[1], getpid());

  request_queue_init(&request_queue, sizeof(struct http_request));
  tpool_init(&tpool, tpool_size, &request_queue, http_server);
  cpu_util_init(&cpu_util);
  ktimer_init(&ktimer, 1000, ktimer_callback, NULL);

  length = sizeof(cli_addr);
  for(;;) {
    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "System call", "accept", 0);
    }
    else {
      struct http_request *r;
      r = request_queue_create_request(&request_queue);
      r->state = REQ_NEW;
      r->socketfd = socketfd;
      r->req_length = 0;
      r->ibuf_length = 0;
      if(tpool_size == 0)
        http_server(&request_queue, &r->req);
      else {
        request_queue_enqueue_request(&request_queue, &r->req);
        tpool_wake(&tpool, 1);
      }
    }
  }
}

static void sig_int(int signo)
{
  if(signo == SIGINT) {
    ktimer_stop(&ktimer);
    cpu_util_fini(&cpu_util);
    close(listenfd);
    print_lifetime_statistics();
    exit(0);
  }
}

static void sig_pipe(int signo)
{
  if(signo == SIGPIPE) {
    logger(LOG, "SIGPIPE caught.", "", 0);
  }
}

static void ktimer_callback(void *arg)
{
  rqstats_prev = rqstats_curr;
  tpstats_prev = tpstats_curr;
  custats_prev = custats_curr;

  rqstats_curr = request_queue_get_stats(&request_queue);
  tpstats_curr = tpool_get_stats(&tpool);
  custats_curr = cpu_util_get_stats(&cpu_util);

  print_interval_statistics();
}

static void print_statistics(struct request_queue_stats *rqprev,
                             struct request_queue_stats *rqcurr,
                             struct tpool_stats *tpprev,
                             struct tpool_stats *tpcurr,
                             struct cpu_util_stats *cuprev,
                             struct cpu_util_stats *cucurr)
{
  printf("Thread Pool Size: %d\n", tpool.size);
  request_queue_print_total_enqueued(rqprev, rqcurr);
  tpool_print_requests_processed(tpprev, tpcurr);
  tpool_print_average_active_threads(tpprev, tpcurr);
  request_queue_print_average_size(rqprev, rqcurr);
  request_queue_print_average_wait_time(rqprev, rqcurr);
  tpool_print_average_processing_time(tpprev, tpcurr);
  cpu_util_print_average_load(cuprev, cucurr);
}

static void print_interval_statistics()
{
  printf("\n");
  printf("Interval Average Statistics:\n");
  printf("Interval Length: %ld\n", ktimer.period_ms);
  print_statistics(&rqstats_prev, &rqstats_curr,
                   &tpstats_prev, &tpstats_curr,
                   &custats_prev, &custats_curr);
}

static void print_lifetime_statistics()
{
  printf("\n");
  printf("Lifetime Average Statistics:\n");
  print_statistics(&((struct request_queue_stats){0}), &rqstats_curr,
                   &((struct tpool_stats){0}), &tpstats_curr,
                   &((struct cpu_util_stats){0}), &custats_curr);
}

