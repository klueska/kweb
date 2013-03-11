#ifndef NWEB_H
#define NWEB_H

#ifndef DEBUG
#define logger(type, s1, s2, socket_fd) ({type;})
#else
void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE*2];

	switch (type) {
		case ERROR:
            sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",
                    s1, s2, errno, getpid()); 
			break;
		case FORBIDDEN: 
			write(socket_fd, page_data[FORBIDDEN_PAGE], 271);
			sprintf(logbuffer,"FORBIDDEN: %s:%s", s1, s2); 
			break;
		case NOTFOUND: 
			write(socket_fd, page_data[NOTFOUND_PAGE], 224);
			sprintf(logbuffer,"NOT FOUND: %s:%s", s1, s2); 
			break;
		case LOG:
			sprintf(logbuffer," INFO: %s:%s:%d", s1, s2, socket_fd);
            break;
	}	
	/* No checks here, nothing can be done with a failure anyway */
	if((fd = open("nweb.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
		write(fd, logbuffer, strlen(logbuffer)); 
		write(fd, "\n", 1);      
		close(fd);
	}
}
#endif // DEBUG
#endif // NWEB_H
