#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <archive.h>
#include <archive_entry.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <Epeg.h>
#include <assert.h>
#include "thumbnails.h"

struct thumbnail_props {
	int size;
	int type;	
	int quality;
};

#define THUMBNAIL_SQUARE 1
#define THUMBNAIL_SCALED 2
#define num_thumbnails (sizeof(thumbnail_props)/sizeof(thumbnail_props[0]))
static struct thumbnail_props thumbnail_props[] = {
	{ 75,   THUMBNAIL_SQUARE, 100 },
	{ 150,  THUMBNAIL_SQUARE, 100 },
	{ 100,  THUMBNAIL_SCALED, 100 },
	{ 320,  THUMBNAIL_SCALED, 100 },
	{ 500,  THUMBNAIL_SCALED, 100 },
	{ 800,  THUMBNAIL_SCALED, 100 },
	{ 1024, THUMBNAIL_SCALED, 100 },
	{ 1600, THUMBNAIL_SCALED, 100 },
	{ 2048, THUMBNAIL_SCALED, 100 }
};

struct thumbnail_data {
	char *inbasename;
	unsigned char *instream;
	int insize;
	unsigned char *outstream;
	int outsize;
	struct thumbnail_props *props;
	char outname[256];
};

static void get_thumbnail_dims(Epeg_Image *input,
                               struct thumbnail_props *props,
                               int *w, int *h)
{
	switch (props->type) {
		case THUMBNAIL_SQUARE:
			*w = props->size;
			*h = props->size;
			break;
		case THUMBNAIL_SCALED:
			epeg_size_get(input, w, h);
			if (*w > *h) {
				*h = (props->size * (*h))/(*w);
				*w = props->size;
			} else if (*h > *w) {
				*w = (props->size * (*w))/(*h);
				*h = props->size;
			} else {
				*w = props->size;
				*h = props->size;
			}
			break;
		default:
			fprintf(stderr, "Incorrect thumbnail type!\n");
			abort();
	}
}

static void *gen_thumbnail(void *arg)
{
	int w, h;
	struct thumbnail_data *td = (struct thumbnail_data*)arg;
	Epeg_Image *input = epeg_memory_open(td->instream, td->insize);
	get_thumbnail_dims(input, td->props, &w, &h);
	sprintf(td->outname, "%s-%dx%d.jpg", td->inbasename, w, h);
	epeg_decode_size_set(input, w, h);
	epeg_quality_set(input, td->props->quality);
	epeg_memory_output_set(input, &td->outstream, &td->outsize);
	epeg_encode(input);
	epeg_close(input);
}

static ssize_t archive_memory_write(struct archive *a, void *__data,
                                    const void *buff, size_t length)
{
	struct thumbnails_file_data *data = (struct thumbnails_file_data *)__data;
	if (data->capacity == 0) {
		data->capacity = length;
		data->stream = malloc(data->capacity);
	} else if (data->size + length > data->capacity) {
		data->capacity *= 2;
		data->stream = realloc(data->stream, data->capacity);
	}
	memcpy(&data->stream[data->size], buff, length);
	data->size += length;
	return length;
}

static void write_entry(struct archive *a, char *name, char *stream, size_t size)
{
	struct archive_entry *entry = archive_entry_new();
	archive_entry_set_pathname(entry, name);
	archive_entry_set_size(entry, size);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0644);
	archive_write_header(a, entry);
	archive_write_data(a, stream, size);
	archive_entry_free(entry);
}

void *write_archive(struct thumbnails_file_data *indata,
                    struct thumbnails_file_data *outdata,
                    struct thumbnail_data *td)
{
	struct archive *a;
	outdata->stream = NULL;
	outdata->size = 0;
	outdata->capacity = 0;

	a = archive_write_new();
	archive_write_add_filter_gzip(a);
	archive_write_set_format_pax_restricted(a);
	archive_write_open(a, outdata, NULL, archive_memory_write, NULL);
	write_entry(a, indata->filename, indata->stream, indata->size);
	for (int i = 0; i < num_thumbnails; i++) {
		write_entry(a, td[i].outname, td[i].outstream, td[i].outsize);
		free(td[i].outstream);
	}
	archive_write_close(a);
	archive_write_free(a);
}

void archive_thumbnails(struct thumbnails_file_data *indata,
                        struct thumbnails_file_data *outdata)
{
	struct thumbnail_data td[num_thumbnails];
	pthread_t threads[num_thumbnails];

	/* Create new string to hold the basefilename from the intpufilename. */
	int len = 0;
	char *inbasename = NULL;
	char *dotindex = rindex(indata->filename, '.');
	if (dotindex != NULL)
		len = dotindex - indata->filename;
	else
		len = strlen(indata->filename);
	inbasename = malloc(len + 1);
	memcpy(inbasename, indata->filename, len);
	inbasename[len] = '\0';

	/* Create the thumbnails. */
	for (int i = 0; i < num_thumbnails; i++) {
		td[i].inbasename = inbasename;
		td[i].instream = indata->stream;
		td[i].insize = indata->size;
		td[i].outstream = NULL;
		td[i].outsize = 0;
		td[i].props = &thumbnail_props[i];
		pthread_create(&threads[i], NULL, gen_thumbnail, &td[i]);
	}
	for (int i = 0; i < num_thumbnails; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Write out an archive of all the thumbnails to a file. */
	outdata->filename = malloc(strlen(inbasename) +
                               strlen("-thumbnails.tgz") + 1);
	sprintf(outdata->filename, "%s-%s", inbasename, "thumbnails.tgz");
	write_archive(indata, outdata, td);

	/* Free the basefilname. */
	free(inbasename);
}

