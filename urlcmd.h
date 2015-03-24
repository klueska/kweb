#ifndef URLCMD_H
#define URLCMD_H

#include <stdbool.h>
#include "tpool.h"

/* Struct representing query parameters */
struct query_param {
	char *key;
	char *value;
};
#define MAX_PARAMS 100

/* Helper functions. */
/* Need to free result of parse_query_string() when done. */
char *find_query_string(char* url);
struct query_param *parse_query_string(char* query);

/* The top level function that knows how to intercept a url and run commands.
 * It returns a buffer of data that should be written back over the connection.
 * This buffer must be freed by the caller. */
char *intercept_url(char *url);

/* Individual commands to run */
char *start_measurements(void *params);
char *stop_measurements(void *params);
char *add_vcores(void *params);
char *yield_pcores(void *params);
char *terminate(void *params);

#endif // URLCMD_H 
