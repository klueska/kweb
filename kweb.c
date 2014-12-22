#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

//#define DEBUG
#include "kweb.h"
#include "tpool.h"
#include "kqueue.h"
#include "cpu_util.h"
#include "kstats.h"
#include "tsc.h"
#include "os.h"
#include "urlcmd.h"

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
  "Date: %s\r\n"
  "Content-Type: %s\r\n"
  "Content-Length: %ld\r\n"
  "Last-Modified: %s\r\n"
  "Connection: keep-alive\r\n"
  "ETag: 54853059-0\r\n"
  "Accept-Ranges: bytes\r\n\r\n",

  "<html><head>\n"
  "<title>Kweb URL Command</title>\n"
  "</head><body>\n"
  "<h1>Kweb URL Command: %s</h1>\n"
  "%s\n"
  "</body></html>\n",
};

void sig_int(int signo);
void sig_pipe(int signo);

struct tpool tpool;
struct kstats kstats;
static int listenfd;
static struct kqueue kqueue;
static struct cpu_util cpu_util;
static struct server_stats server_stats = {0};

void setDateString(time_t *timeval, char *buf)
{
  static const char *DAY_NAMES[] =
    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char *MONTH_NAMES[] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

  time_t __time;
  time_t *t  = &__time;
  struct tm tm;
  if (timeval == NULL)
    time(t);
  else
    t = timeval;
  gmtime_r(t, &tm);

  strftime(buf, RFC1123_TIME_LEN+1, "---, %d --- %Y %H:%M:%S GMT", &tm);
  memcpy(buf, DAY_NAMES[tm.tm_wday], 3);
  memcpy(buf+8, MONTH_NAMES[tm.tm_mon], 3);
}

static ssize_t serialized_write(struct http_connection *c,
                                const void *buf, size_t count)
{
  tpool_inform_blocking(&tpool);
  pthread_mutex_lock(&c->writelock);
  tpool_inform_unblocked(&tpool);
  ssize_t ret = timed_write(c, buf, count);
  pthread_mutex_unlock(&c->writelock);
  return ret;
}

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

static int get_request_length(char *src, int max_len)
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

static int extract_request(struct http_connection *c,
                           struct http_request *r)
{
  int ret = 0;
  while(1) {
    /* Find a request in the connection buf */
    int len = get_request_length(c->buf, c->buf_length);
    
    /* If we found one, update some variables and return to process it */
    if(len > 0) {
      r->length = len;
      c->buf_length = c->buf_length - len;
      memcpy(r->buf, c->buf, r->length);
      memmove(c->buf, &c->buf[r->length], c->buf_length);
      return len;
    }

    /* Otherwise, try and read in the next request from the socket */
    ret = timed_read(c, &c->buf[c->buf_length], sizeof(c->buf) - c->buf_length);

    /* If the return code is invalid or marks the end of a connection, just
     * return it. */
    if(ret <= 0)
      return ret;

    /* Otherwise, update the buf_length and loop back around to try and
     * extract the request again. */
    c->buf_length += ret;
  }
}

static void enqueue_connection_tail(struct kqueue *q,
                                    struct http_connection *c)
{
  __sync_fetch_and_add(&c->ref_count, 1);
  kqueue_enqueue_item_tail(q, &c->conn);
  tpool_wake(&tpool, 1);
}

static void enqueue_connection_head(struct kqueue *q,
                                    struct http_connection *c)
{
  __sync_fetch_and_add(&c->ref_count, 1);
  kqueue_enqueue_item_head(q, &c->conn);
  tpool_wake(&tpool, 1);
}

static bool maybe_destroy_connection(struct kqueue *q,
                                     struct http_connection *c)
{
  int count = __sync_add_and_fetch(&c->ref_count, -1);
  if(count == 0) {
    close(c->socketfd);
    destroy_connection(c);
    kqueue_destroy_item(q, &c->conn);
  }
}

/* This is a child web server thread */
void http_server(struct kqueue *q, struct kitem *__c)
{
  struct http_connection *c = (struct http_connection *)__c;
  struct http_request r;
  struct stat statbuf;
  int j, file_fd, buflen;
  long i = 0, ret = 0;
  char *fstr;
  char *request_line;
  char *saveptr;
  char now[RFC1123_TIME_LEN + 1];
  char mod[RFC1123_TIME_LEN + 1];

  /* Try and extract a request from the connection. */
  ret = extract_request(c, &r);

  /* If there was an error, just destroy the connection, as there is
   * nothing more we can do with this connection anyway. */
  if(ret < 0) {
    logger(LOG, "Connection reset by peer.", "", c->conn.id);
    maybe_destroy_connection(q, c);
    return;
  }

  /* If there was no error, but we weren't able to extract a request, finish up
   * if we are the last one to look at the connection. */
  if(ret == 0) {
    maybe_destroy_connection(q, c);
    return;
  }

  /* Otherwise, just reenqueue the connection so another thread can grab the
   * next request and start processing it. */
  if(c->burst_length) {
    c->burst_length--;
    enqueue_connection_head(q, c);
  }
  else {
    c->burst_length = MAX_BURST;
    enqueue_connection_tail(q, c);
  }

  /* Now parse through the extracted request, grabbing only what we care about */
  request_line = strtok_r(r.buf, "\r\n", &saveptr);
  if (!request_line) {
    logger(LOG, "Unterminated request buffer.", "", c->conn.id);
    maybe_destroy_connection(q, c);
    return;
  }

  /* Make sure it's a GET operation */
  if(strncmp(request_line, "GET ", 4) && strncmp(request_line, "get ", 4)) {
    logger(FORBIDDEN, "Only simple GET operation supported", request_line, c->socketfd);
    serialized_write(c, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
    maybe_destroy_connection(q, c);
    return;
  }
  logger(LOG, "Request", request_line, c->conn.id);

  /* Strip the version info from the request_line */
  for(i=4; i<strlen(request_line); i++) {
    /* String is "GET URL?<query_data> HTTP_VERSION" */
    if(request_line[i] == ' ') {
      request_line[i] = '\0';
      break;
    }
  }

  /* Intercept certain urls and do something special. */
  char *intercept_buf = intercept_url(&request_line[4]);
  if(intercept_buf) {
    int buflen = strlen(intercept_buf);
    /* Send the necessary header info + a blank line */
    logger(LOG, "INTERCEPT URL", &request_line[4], c->conn.id);
    setDateString(NULL, now);
    setDateString(NULL, mod);
    sprintf(r.buf, page_data[OK_HEADER], VERSION, now, "text/html", buflen, mod);
    serialized_write(c, r.buf, strlen(r.buf));
    serialized_write(c, intercept_buf, buflen);
    free(intercept_buf);
    maybe_destroy_connection(q, c);
    return;
  }

  /* Strip all query data from the request_line */
  for(i=4; i<strlen(request_line); i++) {
    /* String is "GET URL?<query_data>" */
    if(request_line[i] == '?') {
      request_line[i] = '\0';
      break;
    }
  }

  /* Otherwise, check for illegal parent directory use .. */
  for(j=4; j<i-1; j++) {
    if(request_line[j] == '.' && request_line[j+1] == '.') {
      logger(FORBIDDEN, "Parent directory (..) path names not supported", request_line, c->socketfd);
      serialized_write(c, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
      maybe_destroy_connection(q, c);
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
    int len = strlen(extensions[i].ext);
    if(!strncmp(&request_line[buflen-len], extensions[i].ext, len)) {
      fstr =extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0) {
    logger(FORBIDDEN, "File extension type not supported", request_line, c->socketfd);
    serialized_write(c, page_data[FORBIDDEN_PAGE], strlen(page_data[FORBIDDEN_PAGE]));
    maybe_destroy_connection(q, c);
    return;
  }

  /* Open the file for reading */
  if((file_fd = open(&request_line[5], O_RDONLY)) == -1) {
    logger(NOTFOUND, "Failed to open file", &request_line[5], c->socketfd);
    serialized_write(c, page_data[NOTFOUND_PAGE], strlen(page_data[NOTFOUND_PAGE]));
    maybe_destroy_connection(q, c);
    return;
  }

  /* Get the File Stats */
  fstat(file_fd, &statbuf);

  /* Prepopulate the request buf with the beginning of the requested file */
  ret = read(file_fd, r.buf, sizeof(r.buf));

  /* Prepare the header info + a blank line */
  setDateString(NULL, now);
  setDateString(&statbuf.st_mtime, mod);
  sprintf(r.rsp_header, page_data[OK_HEADER], VERSION, now,
          fstr, statbuf.st_size, mod); 
  logger(LOG, "Header", r.rsp_header, c->conn.id);
  
  /* Start sending a response */
  logger(LOG, "SEND", &request_line[5], c->conn.id);
  tpool_inform_blocking(&tpool);
  pthread_mutex_lock(&c->writelock);
  tpool_inform_unblocked(&tpool);
  timed_write(c, r.rsp_header, strlen(r.rsp_header));
  /* Send the file itself in 8KB chunks - last block may be smaller */
  do {
    if(timed_write(c, r.buf, ret) < 0)
      logger(LOG, "Write error on socket.", "", c->socketfd);
  } while((ret = read(file_fd, r.buf, sizeof(r.buf))) > 0);
  pthread_mutex_unlock(&c->writelock);

  close(file_fd);
  maybe_destroy_connection(q, c);
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
    if(tpool_size < 1 || tpool_size > INT_MAX) { 
      printf("ERROR: Invalid tpool size %d. Must be >= 1.\n", tpool_size);
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

  /* Do some os specific app initialization */
  os_app_init();

  /* Initialize necessary data structures */
  kqueue_init(&kqueue, sizeof(struct http_connection));
  tpool_init(&tpool, tpool_size, &kqueue, http_server, KWEB_STACK_SZ);
  cpu_util_init(&cpu_util);
  kstats_init(&kstats, &kqueue, &tpool, &cpu_util);

  /* Get the TSC frequency for our timestamp measurements */
  server_stats.tsc_freq = get_tsc_freq();

  /* Start accepting requests and processing them */
  fflush(stdout);
  logger(LOG, "Starting kweb", argv[1], getpid());
  printf("Server Started\n");
  printf("Thread Pool Size: %d\n", tpool.size);
  printf("TSC Frequency: %lu\n", server_stats.tsc_freq);
  length = sizeof(cli_addr);
  for(;;) {
    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0) {
      logger(ERROR, "System call", "accept", 0);
    }
    else {
      struct http_connection *c;
      c = kqueue_create_item(&kqueue);
      c->burst_length = MAX_BURST;
      c->ref_count = 0;
      c->socketfd = socketfd;
      c->buf_length = 0;
      pthread_mutex_init(&c->writelock, NULL);
      init_connection(c);
      enqueue_connection_tail(&kqueue, c);
    }
  }
}

void sig_int(int signo)
{
  if(signo == SIGINT) {
    kstats_stop(&kstats);
    cpu_util_fini(&cpu_util);
    close(listenfd);
    kstats_print_lifetime_statistics(&kstats);
    printf("Server Terminated\n");
    exit(0);
  }
}

void sig_pipe(int signo)
{
  if(signo == SIGPIPE) {
    logger(LOG, "SIGPIPE caught.", "", 0);
  }
}

