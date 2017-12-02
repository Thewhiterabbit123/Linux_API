#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#define EPOLLSIZE 10
#define PORT 5000
#define IPSIZE 20
#define BUFFSIZE 1024
#define TRUE 1
#define FALSE 0

//to stop server on ctrl-c, del all the file descriptor and free all memory without still reachable
static int cycle = 1;
typedef void (*sighandler)(int);
void brake(int c) {
    cycle = 0;
}


struct clients {
    int   write;          //pointer for ring buffer
    int   read;           //pointer for ring buffer
    char  data[BUFFSIZE]; //ring buffer to save data for slow clients
    int   fd;
    int   unused;         //if we can't send data to client, unused = 0, when we have EPOLLOUT try to send again
    char  tmp_ip[IPSIZE];
};

struct clients* initcl() {
    struct clients* cl = (struct clients*) malloc (sizeof(struct clients)*EPOLLSIZE);
    if (!cl) {
        syslog(LOG_INFO,"Error calling malloc %s", strerror(errno));
        return NULL;
    }

    for (int i = 0; i < EPOLLSIZE; ++i) {
        cl[i].write = cl[i].read = 0;
    }
    return cl;
}

int socknonblock(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if(flags == -1) {
        return errno;
    }
    flags |= O_NONBLOCK;
    if(fcntl(sockfd, F_SETFL, flags) < 0) {
        return errno;
    }
    return EXIT_SUCCESS;
}

int createbind() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        syslog(LOG_INFO,"Can't open socket %s", strerror(errno));
        return EXIT_FAILURE;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd,(struct sockaddr *)&serv_addr,sizeof(struct sockaddr)) < 0) {
        syslog(LOG_INFO,"Error calling bind %s", strerror(errno));
        close(sockfd);
        return EXIT_FAILURE;
    }

    if(socknonblock(sockfd) != EXIT_SUCCESS) {
        syslog(LOG_INFO,"Can't make socket nonblock %s", strerror(errno));
        close(sockfd);
        return EXIT_FAILURE;
    }
    return sockfd;
}

void writelog(int fd, char* first, char* second) {
    int len1 = strlen(first);
    write(fd, first, len1);
    write(fd, "  ", 2);
    int len2 = strlen(second);
    write(fd, second, len2);
    write(fd, "\n", 1);
}

int writetosocket(int fd, struct clients* cl, char* buf, int n) {
    cl->unused = TRUE;
    for (int i = 0; i < n; ++i) {
        cl->data[(cl->write++) % BUFFSIZE] = buf[i];
    }

    char* bufftosend = &cl->data[(cl->read) % BUFFSIZE];
    while(cl->unused) {
        int er = send(fd, bufftosend, n, 0);
        if(er == -1 || er == EAGAIN) {
            cl->unused = FALSE;
            if(cl->read == cl->write) {
                syslog(LOG_INFO,"SMth goes wrong\n");
                break;
            }
            return EXIT_FAILURE;
        } 
        if (er > 0) {
            for (int i = cl->read; i < (cl->read + er)%BUFFSIZE ; ++i) {
                cl->data[i] = '\0';
            }
            cl->read = (er + cl->read) % BUFFSIZE;
            return EXIT_SUCCESS;
        }
    }
    return EXIT_SUCCESS;
}

int main() {

    int fd = open("server.log", O_WRONLY | O_APPEND);

    if(fd < 0) {
        syslog(LOG_INFO,"Can't open log file %s", strerror(errno));
        return EXIT_FAILURE;
    }

    int sockfd = createbind();
    if (sockfd == EXIT_FAILURE) {
       close(fd);
       return EXIT_FAILURE;
    }

    int epollfd = epoll_create(EPOLLSIZE);
    struct epoll_event event;
    struct epoll_event events[EPOLLSIZE];
    event.data.fd = sockfd;
    event.events = EPOLLIN|EPOLLET;

    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event) < 0) {
        syslog(LOG_INFO,"Error calling epoll_ctl %s", strerror(errno));
        close(sockfd);
        close(fd);
        return EXIT_FAILURE;
    }
    
    listen(sockfd, EPOLLSIZE);
    
    struct clients* cl = initcl();
    if (!cl) {
        return EXIT_FAILURE;
    }

    int iterator = 0;

    while(cycle) {
        signal(SIGINT,  (sighandler) brake); //CTRL-C
        int n = epoll_wait(epollfd,events,10,-1);
        for(int i = 0; i < n; i++) {
            if(sockfd == events[i].data.fd) {
                while(TRUE) {
                    struct sockaddr_in clientaddr;
                    socklen_t len = sizeof(clientaddr);
                    int connfd = accept(sockfd,(struct sockaddr *)&clientaddr,&len);

                    if(connfd < 0 && errno == EAGAIN) {
                        break;
                    }
                    inet_ntop(AF_INET,&(clientaddr.sin_addr),cl[iterator].tmp_ip, IPSIZE);
                   
                    writelog(fd, "NEW CONNECTION:", cl[iterator].tmp_ip);

                    socknonblock(connfd);
                    event.data.fd = connfd;
                    event.events = EPOLLIN|EPOLLET|EPOLLOUT;
                    epoll_ctl(epollfd,EPOLL_CTL_ADD,connfd,&event);
                    cl[iterator++].fd = connfd;
                }
            } else {
                int connfd = events[i].data.fd;
                char buf[BUFFSIZE];
                while(TRUE) {
                    bzero(buf, BUFFSIZE);
                    int s = read(connfd, buf, BUFFSIZE);
                    if(s <= 0) {
                        if (s == 0) {
                            writelog(fd, "DISCONNECTED:", cl[i].tmp_ip);
                        }
                        if (errno  == EAGAIN) {
                            break;
                        } else {
                            syslog(LOG_INFO,"Error calling read %s", strerror(errno));
                            epoll_ctl(epollfd,EPOLL_CTL_DEL,connfd,NULL);
                            free(cl);
                            close(connfd);
                            close(fd);
                            close(sockfd);
                            return EXIT_FAILURE;
                        }
                    } else {
                        for (int j = 0; j < iterator; ++j) {
                            if (cl[j].fd != connfd) {
                                writetosocket(cl[j].fd, &cl[j], buf, s);
                            }
                        }
                        if (events[i].events & EPOLLOUT) {
                            if (cl[i].unused = FALSE) {
                                writetosocket(cl[i].fd, &cl[i], buf, s);
                            }
                        }
                        writelog(fd, cl[i].tmp_ip, buf);
                    }
                }
            }
        }
    }
    printf("\b\bServer stop. All ok\n");
    free(cl);
    close(sockfd);
    close(fd);
    return EXIT_SUCCESS;
}
