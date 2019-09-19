#include <stdlib.h>		/* for fd_set */
#include <stdio.h>		/* for printf */
#include <fcntl.h>		/* for O_RDONLY | O_NONBLOCK */
#include <sys/ioctl.h>		/* for ioctl */
#include <sys/epoll.h>
#include <string.h>


#define FIFO_CLEAR 0X1

void main(void)
{
	int fd;

	/* 以非阻塞方式打开设备文件 */
	fd = open("/dev/global_mem_0", O_RDONLY | O_NONBLOCK);
	if (fd != -1) {
		struct epoll_event ev_globalfifo;
		int err;
		int epfd;

		epfd = epoll_create(1);
		if (epfd < 0) {
			printf("epoll_create() fail\n");
			return;
		}

		bzero(&ev_globalfifo, sizeof(struct epoll_event));

		/* 设置侦听读事件 */
		ev_globalfifo.events = EPOLLIN | EPOLLPRI;

		/* 将globalfifo 对应的fd加入到侦听的行列 */
		err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev_globalfifo);
		if (err < 0) {
			printf("epoll_ctl() EPOLL_CTL_ADD fail.\n");
			return;
		}

		/* 进行等待，若15秒没有写入设备，则打印 */
		err = epoll_wait(epfd, &ev_globalfifo, 1, 15000);
		if (err < 0) {
			printf("epoll_wait() fail\n");
			return;
		} else if(err == 0){
			printf("No data input in fifo within 15 seconds\n");
		} else {
			printf("FIFO ni not empty\n");
		}

		err = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ev_globalfifo);
		if (err < 0) {
			printf("epoll_ctl() EPOLL_CTL_DEL fail.\n");
		}
	} else {
		printf("fail to open device.\n");
	}
}
