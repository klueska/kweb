#ifndef THUMBNAILS_H
#define THUMBNAILS_H

enum thumbnail_methods {
	THUMBNAILS_SERIAL,
	THUMBNAILS_PTHREADS,
	THUMBNAILS_LITHE_FORK_JOIN,
};

struct thumbnails_file_data {
	char *filename;
	char *stream;
	size_t size;
	size_t capacity;
};

void archive_thumbnails(struct thumbnails_file_data *indata,
                        struct thumbnails_file_data *outdata,
                        int method);
#endif // THUMBNAILS_H

