#ifndef URLCMD_H
#define URLCMD_H

#include <stdbool.h>
#include "tpool.h"

/* The top level function that knows how to intercept a url and run commands */
bool intercept_url(char *url);

/* Individual commands to run */
void start_measurements(void *params);
void stop_measurements(void *params);
void terminate(void *params);

#endif // URLCMD_H 
