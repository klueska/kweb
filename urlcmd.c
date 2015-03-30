#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "urlcmd.h"
#include "kstats.h"
#include "kweb.h"
#include "thumbnails.h"

/* Extern in some global variables for the url commands */
extern struct tpool tpool;
extern struct kstats kstats;
extern void sig_int(int signo);
extern char *page_data[];

/* Struct representing the url commands */
struct url_cmd {
	char name[256];
	void (*func)(struct intercept_buf *ib,
	             struct query_params *params,
	             struct http_request *r);
	bool wrapped;
	char *mime_type;
};

/* Arrays of available commands */
static struct url_cmd get_cmds[] = {
	{"start_measurements", start_measurements, true, "text/html"},
	{"stop_measurements",  stop_measurements,  true, "text/html"},
	{"add_vcores",         add_vcores,         true, "text/html"},
	{"yield_pcores",       yield_pcores,       true, "text/html"},
	{"terminate",          terminate,          true, "text/html"}
};
static struct url_cmd put_cmds[] = {
	{"generate_thumbnails_serial",   generate_thumbnails_serial,   false, "application/x-compressed"},
	{"generate_thumbnails_pthreads", generate_thumbnails_pthreads, false, "application/x-compressed"},
#ifdef WITH_LITHE
	{"generate_thumbnails_lithe",    generate_thumbnails_lithe,    false, "application/x-compressed"},
#else
	{"generate_thumbnails_lithe",    generate_thumbnails_lithe,    true, "text/html"},
#endif

};

/* Find the beginning of the query string, or the end of the url */
static char *find_query_string(char *request_line)
{
	char *c = request_line;
	while(*c != '?' && *c != '\0' && !(*c == '\r' && *(c+1) == '\n'))
		c++;
	if(*c == '?')
		return c+1;
	return NULL;
}

/* Parse a query string into an array of (key, value) pairs */
static struct query_params *parse_query_string(char* request_line)
{
	/* Find the beginning of the query string. */
	char *query = find_query_string(request_line);
	if (query == NULL)
		return NULL;

	/* Find the space at the end of the query string. */
	char *c = strstr(query, " ");
	if ((int)(c - query) > BUFSIZE)
		return NULL;

	struct query_params *qps = calloc(1, sizeof(struct query_params));
	strncpy(qps->src, query, (int)(c - query));
	char *saveptr;
	char *token = strtok_r(qps->src, "&", &saveptr);
	int i = 0;
	while (token != NULL) {
		char *saveptr2;
		qps->p[i].key = strtok_r(token, "=", &saveptr2);
		qps->p[i].value = strtok_r(NULL, "=", &saveptr2);
		token = strtok_r(NULL, "&", &saveptr);
		i++;
	}
	return qps;
}

static void wrap_results(struct intercept_buf *ib, struct url_cmd *cmd)
{
	void *buf = ib->buf;
	size_t size = buf ? strlen(buf) : 6; /* (null) */
	ib->buf = malloc(strlen(page_data[URLCMD_PAGE]) +
	                 strlen(cmd->name) + size + 1);
	sprintf(ib->buf, page_data[URLCMD_PAGE], cmd->name, buf);
	ib->size = strlen(ib->buf);
	free(buf);
}

/* The top level function that knows how to intercept a url and run commands */
bool intercept_request(struct intercept_buf *ib,
                       struct http_request *r)
{
	char *url = NULL;
	struct url_cmd *cmd_list;
	size_t num_cmds;

	if(!strncmp(r->buf, "GET ", 4) && strncmp(r->buf, "get ", 4)) {
		url = &r->buf[4];
		if (url[0] == '/')
			url++;
		cmd_list = &get_cmds[0];
		num_cmds = sizeof(get_cmds)/sizeof(struct url_cmd);
	} else if (!strncmp(r->buf, "PUT ", 4) || !strncmp(r->buf, "put ", 4)) {
		url = &r->buf[4];
		if (url[0] == '/')
			url++;
		cmd_list = &put_cmds[0];
		num_cmds = sizeof(put_cmds)/sizeof(struct url_cmd);
	}

	if (!url)
		return false;

	/* Loop through known commands and look for a match */
	for (int i = 0; i < num_cmds; i++) {
		struct url_cmd *cmd = &cmd_list[i];
		/* If we found a match! */
		if (strncmp(url, cmd->name, strlen(cmd->name)) == 0) {
			/* Parse the query string into a set of key/value pairs. */
			struct query_params *params = parse_query_string(url);
			/* Now run the command, passing it its parameters */
			cmd->func(ib, params, r);
			/* Wrap the results if desired. */
			if (cmd->wrapped)
				wrap_results(ib, cmd);
			/* Set the mime-type. */
			ib->mime_type = cmd->mime_type;
			/* Free the params. */
			free(params);
			return true;
		}
	}
	return false;
}

void start_measurements(struct intercept_buf *ib,
                        struct query_params *params,
                        struct http_request *r)
{
	struct {
		unsigned int period_ms;
		int file_size;
		int tpool_size;
	} my_params = {1000, 0, tpool.size};

	if (params) {
		for (int i=0; i < MAX_PARAMS; i++) {
			if (params->p[i].key == NULL)
				break;
			else if (!strcmp(params->p[i].key, "period_ms"))
				my_params.period_ms = atoi(params->p[i].value);
			else if (!strcmp(params->p[i].key, "file_size"))
				my_params.file_size = atoi(params->p[i].value);
			else if (!strcmp(params->p[i].key, "tpool_size"))
				my_params.tpool_size = atoi(params->p[i].value);
		}
	}
	tpool_resize(&tpool, my_params.tpool_size);

	ib->buf = malloc(256);
	char *bp = ib->buf;
	bp += sprintf(bp, "Starting Measurements<br/>");
	bp += sprintf(bp, "Interval Length: %u<br/>", my_params.period_ms);
	bp += sprintf(bp, "Thread Pool Size: %d<br/>", my_params.tpool_size);
	bp += sprintf(bp, "File Size: %d<br/>", my_params.file_size);
	ib->size = bp - (char*)ib->buf;
	kstats_start(&kstats, my_params.period_ms);
} 

void stop_measurements(struct intercept_buf *ib,
                       struct query_params *params,
                       struct http_request *r)
{
	ib->buf = malloc(256);
	char *bp = ib->buf;
	bp += sprintf(ib->buf, "Stopped Measurements<br/>");
	ib->size = bp - (char*)ib->buf;
	kstats_stop(&kstats);
}

void add_vcores(struct intercept_buf *ib,
                struct query_params *params,
                struct http_request *r)
{
	struct {
		int num_vcores;
	} my_params = {-1};

	if (params) {
		for (int i=0; i < MAX_PARAMS; i++) {
			if (params->p[i].key == NULL)
				break;
			else if (!strcmp(params->p[i].key, "num_vcores"))
				my_params.num_vcores = atoi(params->p[i].value);
		}
	}
	ib->buf = malloc(256);
	char *bp = ib->buf;

#if !defined(__ros__) && !defined(WITH_PARLIB)
	bp += sprintf(bp, "Error: only supported on Akaros or Linux With Parlib");
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
	ib->size = bp - (char*)ib->buf;
}

void yield_pcores(struct intercept_buf *ib,
                  struct query_params *params,
                  struct http_request *r)
{
	int ret;
	struct {
		int pcoreid;
	} my_params = {-1};

	if (params) {
		for (int i=0; i < MAX_PARAMS; i++) {
			if (params->p[i].key == NULL)
				break;
			else if (!strcmp(params->p[i].key, "pcoreid"))
				my_params.pcoreid = atoi(params->p[i].value);
		}
	}
	ib->buf = malloc(256);
	char *bp = ib->buf;

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
	ib->size = bp - (char*)ib->buf;
}

void terminate(struct intercept_buf *ib,
               struct query_params *params,
               struct http_request *r)
{
	sig_int(SIGINT);
}

static void generate_thumbnails(struct intercept_buf *ib,
                                struct query_params *params,
                                struct http_request *r,
                                int method)
{
#if !defined(__akaros__) && !defined(WITH_CUSTOM_SCHED)
	struct thumbnails_file_data indata, outdata;
	char *default_filename = "";

	indata.filename = NULL;
	if (params) {
		for (int i=0; i < MAX_PARAMS; i++) {
			if (params->p[i].key == NULL)
				break;
			else if (!strcmp(params->p[i].key, "file"))
				indata.filename = params->p[i].value;
		}
	}
	if (indata.filename == NULL)
		indata.filename = default_filename;
	indata.stream = &r->buf[r->header_length];
	indata.size = r->length - r->header_length;
	indata.capacity = indata.size;
	archive_thumbnails(&indata, &outdata, method);
	free(outdata.filename);
	ib->buf = outdata.stream;
	ib->size = outdata.size;
#endif
}

void generate_thumbnails_serial(struct intercept_buf *ib,
                                struct query_params *params,
                                struct http_request *r)
{
	generate_thumbnails(ib, params, r, THUMBNAILS_SERIAL);
}

void generate_thumbnails_pthreads(struct intercept_buf *ib,
                                  struct query_params *params,
                                  struct http_request *r)
{
	generate_thumbnails(ib, params, r, THUMBNAILS_PTHREADS);
}

void generate_thumbnails_lithe(struct intercept_buf *ib,
                               struct query_params *params,
                               struct http_request *r)
{
	generate_thumbnails(ib, params, r, THUMBNAILS_LITHE_FORK_JOIN);
}

