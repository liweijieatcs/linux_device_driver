#include <stdlib.h>		/* for fd_set */
#include <stdio.h>		/* for printf */
#include <fcntl.h>		/* for O_RDONLY | O_NONBLOCK */
#include <sys/ioctl.h>	/* for ioctl */

#define FIFO_CLEAR 0X1
#define BUFFER_LEN 20

void main(void) 
{
	int fd, num;
	char fd_ch[BUFFER_LEN];
	fd_set rfds, wfds;

	fd = open("/dev/global_mem_0", O_RDONLY | O_NONBLOCK);
	if (fd != -1) {
		if (ioctl(fd, FIFO_CLEAR, 0) < 0)
			printf("ioctl comand failed.\n");

		while (1) {
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &rfds);
			FD_SET(fd, &wfds);

			select(fd + 1, &rfds, &wfds, NULL, NULL);

			/* 数据可读 */
			if (FD_ISSET(fd, &rfds)) {
				printf("poll monitor: can be read.\n");
			}

			/* 数据可写 */
			if (FD_ISSET(fd, &wfds)) {
				printf("poll monitor: can be writen.\n");
			}
		}
	} else {
		printf("fail to open device.\n");
	}
}
