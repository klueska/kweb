#ifndef URLCMD_H
#define URLCMD_H

#include <stdbool.h>
#include "tpool.h"
#include "kweb.h"

/* Struct representing query parameters */
#define MAX_PARAMS 100
struct query_params {
	char src[BUFSIZE];
	struct {
		char *key;
		char *value;
	} p[MAX_PARAMS];
};

struct intercept_buf {
	void *buf;
	size_t size;
	char *mime_type;
};

/* The top level function that knows how to intercept an entire request and
 * do something special with it. */
bool intercept_request(struct intercept_buf *ib,
                       struct http_request *r);


/* Individual commands to run */
void start_measurements           (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void stop_measurements            (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void add_vcores                   (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void yield_pcores                 (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void terminate                    (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void generate_thumbnails_serial   (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void generate_thumbnails_pthreads (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);
void generate_thumbnails_lithe    (struct intercept_buf *ib,
                                   struct query_params *params,
                                   struct http_request *r);

#endif // URLCMD_H 
