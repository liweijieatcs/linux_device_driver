#include <stdlib.h>		/* for fd_set */
#include <stdio.h>		/* for printf */
#include <fcntl.h>		/* for O_RDONLY | O_NONBLOCK */
#include <sys/ioctl.h>		/* for ioctl */

#define FIFO_CLEAR 0X1

void main(void)
{
	int fd;
	fd_set rfds, wfds;	/* 读写文件描述符集合 */

	/* 以非阻塞方式打开设备文件 */
	fd = open("/dev/global_mem_0", O_RDONLY | O_NONBLOCK);
	if (fd != -1) {
		/* fifo清零 */
		if (ioctl(fd, FIFO_CLEAR, 0) < 0)
			printf("ioctl comand failed.\n");

		while (1) {
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &rfds);
			FD_SET(fd, &wfds);

			select(fd + 1, &rfds, &wfds, NULL, NULL);

			/* 数据可获得 */
			if (FD_ISSET(fd, &rfds)) {
				printf("poll monitor: can be read.\n");
			}

			/* 数据可写入 */
			if (FD_ISSET(fd, &wfds)) {
				printf("poll monitor: can be writen.\n");
			}
		}
	} else {
		printf("fail to open device.\n");
	}
}
