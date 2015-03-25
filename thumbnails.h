#ifndef THUMBNAILS_H
#define THUMBNAILS_H

struct thumbnails_file_data {
	char *filename;
	void *stream;
	size_t size;
};

void archive_thumbnails(struct thumbnails_file_data *indata,
                        struct thumbnails_file_data *outdata);

#endif // THUMBNAILS_H

