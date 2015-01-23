#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "urlcmd.h"
#include "kstats.h"
#include "kweb.h"

/* Extern in some global variables for the url commands */
extern struct tpool tpool;
extern struct kstats kstats;
extern void sig_int(int signo);
extern char *page_data[];

/* Struct representing the url commands */
struct url_cmd {
	char name[256];
	char *(*func)(void *arg);
};

/* Struct representing query parameters */
struct query_param {
	char *key;
	char *value;
};
#define MAX_PARAMS 100

/* An array of available commands */
static struct url_cmd url_cmds[] = {
	{"start_measurements", start_measurements},
	{"stop_measurements", stop_measurements},
	{"add_vcores", add_vcores},
	{"yield_pcores", yield_pcores},
	{"terminate", terminate}
};

/* Parse a query string into an array of (key, value) pairs */
struct query_param *parse_query_string(char* query) {
	struct query_param *qps = calloc(MAX_PARAMS, sizeof(struct query_param));
	char *saveptr;
	char *token = strtok_r(query, "&", &saveptr);
	int i = 0;
	while (token != NULL) {
		char *saveptr2;
		qps[i].key = strtok_r(token, "=", &saveptr2);
		qps[i].value = strtok_r(NULL, "=", &saveptr2);
		token = strtok_r(NULL, "&", &saveptr);
		i++;
	}
	return qps;
}

/* The top level function that knows how to intercept a url and run commands */
char *intercept_url(char *url) {
	/* Strip off the leading slash if there is one */
	if (url[0] == '/')
		url++;

	/* Loop through known commands and look for a match */
	for (int i=0; i < sizeof(url_cmds)/sizeof(struct url_cmd); i++) {
		/* If we found a match! */
		if (strncmp(url, url_cmds[i].name, strlen(url_cmds[i].name)) == 0) {
			/* Find the beginning of the query string, or the end of the url */
			char *c = url;
			while(*c != '?' && *c != '\0' && *c != ' ')
				c++;
			/* If we found the beginning of a query string, parse it. */
			struct query_param *params = NULL;
			if(*c == '?')
				params = parse_query_string(c+1);
			/* Now run the command, passing it its parameters */
			char *cmdbuf = url_cmds[i].func(params);
			size_t cmdbuflen = cmdbuf ? strlen(cmdbuf) : 6; /* (null) */
			/* And return a buffer with some output */
			char *buf = malloc(strlen(page_data[URLCMD_PAGE]) +
			                   strlen(url_cmds[i].name) + 
			                   cmdbuflen + 1);
			sprintf(buf, page_data[URLCMD_PAGE], url_cmds[i].name, cmdbuf);
			free(cmdbuf);
			free(params);
			return buf;
		}
	}
	/* No matches */
	return NULL;
}

char *start_measurements(void *__params) {
	struct {
		unsigned int period_ms;
		int file_size;
		int tpool_size;
	} my_params = {1000, 0, tpool.size};

	if (__params) {
		struct query_param *p = (struct query_param*)__params;
		for (int i=0; i < MAX_PARAMS; i++) {
			if (p[i].key == NULL)
				break;
			else if (!strcmp(p[i].key, "period_ms"))
				my_params.period_ms = atoi(p[i].value);
			else if (!strcmp(p[i].key, "file_size"))
				my_params.file_size = atoi(p[i].value);
			else if (!strcmp(p[i].key, "tpool_size"))
				my_params.tpool_size = atoi(p[i].value);
		}
	}
	tpool_resize(&tpool, my_params.tpool_size);

	char *buf = malloc(256);
	char *bp = buf;
	bp += sprintf(bp, "Starting Measurements<br/>");
	bp += sprintf(bp, "Interval Length: %u<br/>", my_params.period_ms);
	bp += sprintf(bp, "Thread Pool Size: %d<br/>", my_params.tpool_size);
	bp += sprintf(bp, "File Size: %d<br/>", my_params.file_size);
	kstats_start(&kstats, my_params.period_ms);
	return buf;
} 

char *stop_measurements(void *params) {
	char *buf = malloc(256);
	kstats_stop(&kstats);
	sprintf(buf, "Stopped Measurements<br/>");
	return buf;
}

char *add_vcores(void *__params) {
	struct {
		int num_vcores;
	} my_params = {-1};

	if (__params) {
		struct query_param *p = (struct query_param*)__params;
		for (int i=0; i < MAX_PARAMS; i++) {
			if (p[i].key == NULL)
				break;
			else if (!strcmp(p[i].key, "num_vcores"))
				my_params.num_vcores = atoi(p[i].value);
		}
	}
	char *buf = malloc(256);
	char *bp = buf;

#if !defined(__ros__) && !defined(WITH_UPTHREAD)
	bp += sprintf(bp, "Error: only supported on Akaros or Linux Upthread");
#else
	if (my_params.num_vcores < -1) {
		bp += sprintf(bp, "Error: you must specify a query srtring parameter "
		                  "of \"num_vcores=\" that is > 0");
	} else {
		vcore_request(my_params.num_vcores);
		bp += sprintf(bp, "Success: you now have requests granted or pending "
		                  "for %d vcores.",
#ifdef __ros__
                              __procdata.res_req[RES_CORES].amt_wanted);
#else
                             (int)num_vcores());
#endif
	}
#endif
	return buf;
}

char *yield_pcores(void *__params) {
	int ret;
	struct {
		int pcoreid;
	} my_params = {-1};

	if (__params) {
		struct query_param *p = (struct query_param*)__params;
		for (int i=0; i < MAX_PARAMS; i++) {
			if (p[i].key == NULL)
				break;
			else if (!strcmp(p[i].key, "pcoreid"))
				my_params.pcoreid = atoi(p[i].value);
		}
	}
	char *buf = malloc(256);
	char *bp = buf;

#if !defined(WITH_CUSTOM_SCHED)
	bp += sprintf(bp, "Error: only supported on Akaros and Linux with Custom Schedulers");

#else
	if (my_params.pcoreid < -1) {
		bp += sprintf(bp, "Error: you must specify a query srtring parameter "
		                  "of \"pcoreid=\" that is > 0");
	} else {
		ret = yield_pcore(my_params.pcoreid);
		udelay(10000); /* give some time for the yield to kick in, might fail */
		if (!ret)
			bp += sprintf(bp, "Success: you now have %d vcores wanted",
#ifdef __ros__
			              __procdata.res_req[RES_CORES].amt_wanted);
#else
                          (int)num_vcores());
#endif
		else
			bp += sprintf(bp, "Something failed: you have %d vcores wanted",
#ifdef __ros__
			              __procdata.res_req[RES_CORES].amt_wanted);
#else
                          (int)num_vcores());
#endif
	}
#endif
	return buf;
}

char *terminate(void *params) {
	sig_int(SIGINT);
	return NULL;
}

