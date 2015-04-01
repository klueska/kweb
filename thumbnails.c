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
#include <magick/api.h>
#include <assert.h>
#include <parlib/timing.h>
#include "thumbnails.h"

#ifdef WITH_LITHE
#include <lithe/lithe.h>
#include <lithe/fork_join_sched.h>
#endif

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
	char *instream;
	size_t insize;
	char *outstream;
	size_t outsize;
	struct thumbnail_props *props;
	char outname[256];
};

static void get_thumbnail_dims(struct thumbnail_props *props,
                               int inw, int inh,
                               int *outw, int *outh)
{
	switch (props->type) {
		case THUMBNAIL_SQUARE:
			*outw = props->size;
			*outh = props->size;
			break;
		case THUMBNAIL_SCALED:
			if (inw > inh) {
				*outh = (props->size * inh)/inw;
				*outw = props->size;
			} else if (inh > inw) {
				*outw = (props->size * inw)/inh;
				*outh = props->size;
			} else {
				*outw = props->size;
				*outh = props->size;
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
	ExceptionInfo excp;
	GetExceptionInfo(&excp);
	ImageInfo *inimg_info = CloneImageInfo((ImageInfo *) NULL);
	ImageInfo *outimg_info = CloneImageInfo((ImageInfo *) NULL);
	Image *inimg = BlobToImage(inimg_info, td->instream, td->insize, &excp);
	get_thumbnail_dims(td->props, inimg->columns, inimg->rows, &w, &h);

	sprintf(td->outname, "%s-%dx%d.jpg", td->inbasename, w, h);
	Image *outimg = ThumbnailImage(inimg, w, h, &excp);
	td->outstream = ImageToBlob(outimg_info, outimg, &td->outsize, &excp);

	DestroyExceptionInfo(&excp);
	DestroyImage(inimg);
	DestroyImage(outimg);
	DestroyImageInfo(inimg_info);
	DestroyImageInfo(outimg_info);
}

static int gen_thumbnails_serial(struct thumbnail_data *td)
{
	int w, h;
	ExceptionInfo excp;
	ImageInfo img_info;
	GetExceptionInfo(&excp);
	GetImageInfo(&img_info);
	Image *inimg = BlobToImage(&img_info, td->instream, td->insize, &excp);

	for (int i=0; i < num_thumbnails; i++) {
		GetImageInfo(&img_info);
		get_thumbnail_dims(td[i].props, inimg->columns, inimg->rows, &w, &h);
		sprintf(td[i].outname, "%s-%dx%d.jpg", td[i].inbasename, w, h);
		Image *outimg = ThumbnailImage(inimg, w, h, &excp);
		td[i].outstream = ImageToBlob(&img_info, outimg, &td[i].outsize, &excp);
		DestroyImage(outimg);
	}

	DestroyExceptionInfo(&excp);
	DestroyImage(inimg);
	return 0;
}

static int gen_thumbnails_pthread(struct thumbnail_data *td)
{
	pthread_t threads[num_thumbnails];
	for (int i=0; i < num_thumbnails; i++) {
		pthread_create(&threads[i], NULL, gen_thumbnail, &td[i]);
	}
	for (int i=0; i < num_thumbnails; i++) {
		pthread_join(threads[i], NULL);
	}
	return 0;
}

static int gen_thumbnails_lithe_fork_join(struct thumbnail_data *td)
{
	#ifndef WITH_LITHE
		char *str = "Sorry, lithe is not supported in this configuration!";
		td->outsize = strlen(str);
		td->outstream = malloc(td->outsize + 1);
		sprintf(td->outstream, "%s", str);
		return -1;
	#else
		lithe_fork_join_sched_t *sched = lithe_fork_join_sched_create();
		lithe_sched_enter(&sched->sched);
		for (int i=0; i < num_thumbnails; i++) {
			void *func = gen_thumbnail;
			lithe_fork_join_context_create(sched, 10*PGSIZE, func, &td[i]);
		}
		lithe_fork_join_sched_join_all(sched);
		lithe_sched_exit();
		lithe_fork_join_sched_destroy(sched);
		return 0;
	#endif
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
                        struct thumbnails_file_data *outdata,
                        int method)
{
	struct thumbnail_data td[num_thumbnails];

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

	/* Set the outfilename */
	outdata->filename = malloc(strlen(inbasename) +
	                           strlen("-thumbnails.tgz") + 1);
	sprintf(outdata->filename, "%s-%s", inbasename, "thumbnails.tgz");

	/* Create the thumbnails. */
	int ret = 0;
	for (int i = 0; i < num_thumbnails; i++) {
		td[i].inbasename = inbasename;
		td[i].instream = indata->stream;
		td[i].insize = indata->size;
		td[i].outstream = NULL;
		td[i].outsize = 0;
		td[i].props = &thumbnail_props[i];
	}
	switch (method) {
		case THUMBNAILS_SERIAL:
			ret = gen_thumbnails_serial(td);
			break;
		case THUMBNAILS_PTHREADS:
			ret = gen_thumbnails_pthread(td);
			break;
		case THUMBNAILS_LITHE_FORK_JOIN:
			ret = gen_thumbnails_lithe_fork_join(td);
			break;
	}

	/* If there was an error. Assume the output buffer is set in the first td
	 * element's outstream. */
	if (ret < 0) {
		outdata->stream = td->outstream;
		outdata->size = td->outsize;
	/* Otherwise, write out an archive of all the thumbnails to a file. */
	} else {
		write_archive(indata, outdata, td);
	}

	/* Free the basefilename. */
	free(inbasename);
}

