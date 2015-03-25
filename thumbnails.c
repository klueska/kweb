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
#include "thumbnails.h"

#define num_thumbnails (sizeof(sizes)/sizeof(sizes[0]))
static const int quality = 100;
static const int sizes[] = { 75, 100, 125, 150, 200, 400, 600 };

struct thumbnail_data {
	char *infilebase;
	unsigned char *instream;
	int insize;
	unsigned char *outstream;
	int outsize;
	int w;
	int h;
	int quality;
	char filename[256];
};

static void *gen_thumbnail(void *arg)
{
	struct thumbnail_data *td = (struct thumbnail_data*)arg;
	sprintf(td->filename, "%s-%dx%d.jpg", td->infilebase, td->w, td->h);
	Epeg_Image *input = epeg_memory_open(td->instream, td->insize);
	epeg_decode_size_set(input, td->w, td->h);
	epeg_quality_set(input, td->quality);
	epeg_memory_output_set(input, &td->outstream, &td->outsize);
	epeg_encode(input);
	epeg_close(input);
}

void *write_archive(struct thumbnails_file_data *data, struct thumbnail_data *td)
{
	struct archive *a;
	struct archive_entry *entry;
	int fd = open(data->filename, O_WRONLY | O_CREAT, 0644);
	data->stream = NULL;
	data->size = 0;

	a = archive_write_new();
	archive_write_add_filter_gzip(a);
	archive_write_set_format_pax_restricted(a);
	archive_write_open_fd(a, fd);
	for (int i = 0; i < num_thumbnails; i++) {
		entry = archive_entry_new();
		archive_entry_set_pathname(entry, td[i].filename);
		archive_entry_set_size(entry, td[i].outsize);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		archive_write_data(a, td[i].outstream, td[i].outsize);
		archive_entry_free(entry);
	}
	archive_write_close(a);
	archive_write_free(a);
	close(fd);
}

void archive_thumbnails(struct thumbnails_file_data *indata,
                        struct thumbnails_file_data *outdata)
{
	struct thumbnail_data td[num_thumbnails];
	pthread_t threads[num_thumbnails];

	/* Truncate the extension from the intpufilename to get the base name. */
	char *dotindex = rindex(indata->filename, '.');
	if (dotindex != NULL)
		*dotindex = '\0';

	/* Create the thumbnails. */
	for (int i = 0; i < num_thumbnails; i++) {
		td[i].infilebase = indata->filename;
		td[i].instream = indata->stream;
		td[i].insize = indata->size;
		td[i].outstream = NULL;
		td[i].outsize = 0;
		td[i].w = sizes[i];
		td[i].h = sizes[i];
		td[i].quality = quality;
		pthread_create(&threads[i], NULL, gen_thumbnail, &td[i]);
	}
	for (int i = 0; i < num_thumbnails; i++) {
		pthread_join(threads[i], NULL);
	}

	/* Write out an archive of all the thumbnails to a file. */
	outdata->filename = malloc(strlen(indata->filename) +
                               strlen("-thumbnails.tgz") + 1);
	sprintf(outdata->filename, "%s-%s", indata->filename, "thumbnails.tgz");
	write_archive(outdata, td);
}

