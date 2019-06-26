/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */
/*
 * pubsub_tcp_socket_handler.c
 *
 *  \date       Mar 1, 2016
 *  \author    	<a href="mailto:dev@celix.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <array_list.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <assert.h>
#include "ctype.h"
#include <netdb.h>
#include <signal.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include "hash_map.h"
#include "utils.h"
#include "pubsub_tcp_handler.h"

#define IP_HEADER_SIZE  20
#define TCP_HEADER_SIZE 20
#define MAX_EPOLL_EVENTS        16
#define MAX_MSG_VECTOR_LEN 256

typedef bool (*pubsub_tcpHandler_dataAvailable_callback_t)(pubsub_tcpHandler_pt handle, int fd,
                                                                 unsigned int *index, unsigned int *size);

struct pubsub_tcpHandler {
  array_list_pt bufferLists;
  unsigned int bufferIdx;
  unsigned int readSeqNr;
  unsigned int writeSeqNr;
  unsigned int msgIdOffset;
  unsigned int mtuSize;
  unsigned int mtuBufferSize;
  bool bypassHeader;
  pthread_mutex_t dbLock;
  unsigned int timeout;
  pubsub_tcpHandler_processMessage_callback_t processMessageCallback;
  void *processMessagePayload;
  hash_map_t *url_map;
  hash_map_t *fd_map;
  bool connected;
  int efd;
  int fd;
};

typedef struct pubsub_tcpBufferPartList {
  pubsub_tcp_msg_header_t default_header;
  unsigned int bufferSize;
  char *buffer;
} *pubsub_tcpBufferPartList_pt;


typedef struct pubsub_tcp_url {
  char *protocol;
  char *hostname;
  unsigned int portnr;
} pubsub_tcp_url_t;

typedef struct psa_tcp_connection_entry {
  char *url;
  int fd;
  struct sockaddr_in addr;
  socklen_t len;
  pubsub_tcpHandler_connectMessage_callback_t disconnectMessageCallback;
  void* payload;
} psa_tcp_connection_entry_t;


typedef struct pubsub_tcp_seq_msg_header {
  unsigned int msg_sequence_number;
  unsigned int total_msg_size;
  unsigned int part_msg_size;
  unsigned int offset;
} pubsub_tcp_seq_msg_header_t;




static inline int pubsub_tcpHandler_set_in_addr(const char *hostname, int port, struct sockaddr_in *inp);
static inline void pubsub_tcpHandler_set_url_info(char *url, pubsub_tcp_url_t *url_info);


//
// Create a handle
//
pubsub_tcpHandler_pt pubsub_tcpHandler_create() {
  pubsub_tcpHandler_pt handle = calloc(sizeof(*handle), 1);
  if (handle != NULL) {
    handle->fd = -1;
    handle->efd = epoll_create1(0);
    handle->bufferIdx = 0;
    handle->url_map = hashMap_create(utils_stringHash, NULL, utils_stringEquals, NULL);
    handle->fd_map = hashMap_create(NULL, NULL, NULL, NULL);
    handle->timeout = 5 * 1000;
    pthread_mutex_init(&handle->dbLock, 0);
    signal(SIGPIPE, SIG_IGN);
  }
  return handle;
}


//
// Destroys the handle
//
void pubsub_tcpHandler_destroy(pubsub_tcpHandler_pt handle) {
  printf("### Destroying BufferHAndler TCP\n");
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    pubsub_tcpHandler_close(handle);
    if (handle->bufferLists != NULL) {
      int listSize = arrayList_size(handle->bufferLists);
      int i;
      for (i = 0; i < listSize; i++) {
        pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, i);
        if (item) {
          if (item->buffer) {
            free(item->buffer);
            item->buffer = NULL;
          }
          free(item);
        }
      }
      arrayList_destroy(handle->bufferLists);
    }
    hashMap_destroy(handle->url_map, false, false);
    hashMap_destroy(handle->fd_map, false, false);
    pubsub_tcpHandler_close(handle);
    if (handle->efd >= 0) close(handle->efd);
    handle->bufferLists = NULL;
    pthread_mutex_unlock(&handle->dbLock);
    pthread_mutex_destroy(&handle->dbLock);
    free(handle);
  }
}

// Destroys the handle
//
int pubsub_tcpHandler_open(pubsub_tcpHandler_pt handle, char *url) {
  int rc = 0;
  pthread_mutex_lock(&handle->dbLock);
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (rc >= 0) {
    int setting = 1;
    if (rc == 0) {
      rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &setting, sizeof(setting));
      if (rc != 0) {
        close(fd);
        fprintf(stderr, "[TCP Socket] Error setsockopt(SO_REUSEADDR): %s\n", strerror(errno));
      }
    }
    if (rc == 0) {
      rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &setting, sizeof(setting));
      if (rc != 0) {
        close(fd);
        fprintf(stderr, "[TCP Socket] Error setsockopt(TCP_NODELAY): %s\n", strerror(errno));
      }
    } else {
      fprintf(stderr, "[TCP Socket] Error creating socket: %s\n", strerror(errno));
    }
    struct sockaddr_in addr; // connector's address information
    pubsub_tcp_url_t url_info;
    pubsub_tcpHandler_set_url_info(url, &url_info);
    rc = pubsub_tcpHandler_set_in_addr(url_info.hostname, url_info.portnr, &addr);
    if (rc == 0) {
      rc = bind(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr));
      if (rc != 0) {
        close(fd);
        fprintf(stderr, "[TCP Socket] Error bind: %s\n", strerror(errno));
      }
    }

    if (url_info.hostname) free(url_info.hostname);
    if (url_info.protocol) free(url_info.protocol);
  }
  pthread_mutex_unlock(&handle->dbLock);
  return (!rc) ? fd : rc;
}


// Destroys the handle
//
int pubsub_tcpHandler_close(pubsub_tcpHandler_pt handle) {
  int rc = 0;
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    if (handle->efd >= 0) {
      struct epoll_event event;
      bzero(&event, sizeof(struct epoll_event)); // zero the struct
      rc = epoll_ctl(handle->efd, EPOLL_CTL_DEL, handle->fd, &event);
      if (rc < 0) {
        fprintf(stderr, "[PSA TCP] Error disconnecting %s\n", strerror(errno));
      }
    }
    if (handle->fd >= 0) {
      close(handle->fd);
    }
    pthread_mutex_unlock(&handle->dbLock);
  }
  return rc;
}

int pubsub_tcpHandler_connect(pubsub_tcpHandler_pt handle, char *url, void* payload, pubsub_tcpHandler_connectMessage_callback_t connectMessageCallback, pubsub_tcpHandler_connectMessage_callback_t disconnectMessageCallback) {
  int fd = pubsub_tcpHandler_open(handle, NULL);
  pubsub_tcp_url_t url_info;
  psa_tcp_connection_entry_t *entry = NULL;
  pthread_mutex_lock(&handle->dbLock);
  int rc = fd;
  struct sockaddr_in addr; // connector's address information
  if (rc >= 0) {
    pubsub_tcpHandler_set_url_info(url, &url_info);
    rc = pubsub_tcpHandler_set_in_addr(url_info.hostname, url_info.portnr, &addr);
    if (rc < 0) {
      fprintf(stderr, "[TCP Socket] Cannot create url\n");
      close(fd);
    }
  }
  if (rc >= 0) {
    rc = connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr));
    if (rc < 0) {
      fprintf(stderr, "[TCP Socket] Cannot connect to %s\n", url_info.hostname);
      close(fd);
      fd = -1;
    } else {
      entry = calloc(1, sizeof(*entry));
      entry->url = strndup(url, 1024 * 1024);
      entry->fd = fd;
      entry->payload = payload;
      entry->disconnectMessageCallback = disconnectMessageCallback;
    }
  }
  if (rc >= 0) {
    struct epoll_event event;
    bzero(&event, sizeof(struct epoll_event)); // zero the struct
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLOUT;
    event.data.fd = entry->fd;
    rc = epoll_ctl(handle->efd, EPOLL_CTL_ADD, entry->fd, &event);
    if (rc < 0) {
      close(entry->fd);
      free(entry->url);
      free(entry);
      fprintf(stderr, "[TCP Socket] Cannot create epoll\n");
    }
  }
  if ((rc >= 0) && (entry)) {
    if (connectMessageCallback) connectMessageCallback(entry->payload);
    hashMap_put(handle->url_map, entry->url, entry);
    hashMap_put(handle->fd_map, (void *) (intptr_t) entry->fd, entry);
  }
  free(url_info.hostname);
  free(url_info.protocol);
  pthread_mutex_unlock(&handle->dbLock);
  return rc;
}

// Destroys the handle
//
int pubsub_tcpHandler_close_connection(pubsub_tcpHandler_pt handle, char *url) {
  int rc = 0;
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    psa_tcp_connection_entry_t *entry = hashMap_remove(handle->url_map, url);
    hashMap_remove(handle->fd_map, (void *) (intptr_t) entry->fd);
    if (handle->efd >= 0) {
      struct epoll_event event;
      bzero(&event, sizeof(struct epoll_event)); // zero the struct
      rc = epoll_ctl(handle->efd, EPOLL_CTL_DEL, entry->fd, &event);
      if (rc < 0) {
        fprintf(stderr, "[PSA TCP] Error disconnecting %s\n", strerror(errno));
      }
    }
    if (entry->fd >= 0) {
      if (entry->disconnectMessageCallback) entry->disconnectMessageCallback(entry->payload);
      close(entry->fd);
      free(entry->url);
      free(entry);
    }
    pthread_mutex_unlock(&handle->dbLock);
  }
  return rc;
}

// Destroys the handle
//
int pubsub_tcpHandler_close_connection_fd(pubsub_tcpHandler_pt handle, int fd) {
  int rc = 0;
  if (handle != NULL) {
    bool use_handle_fd = false;
    psa_tcp_connection_entry_t *entry = NULL;
    pthread_mutex_lock(&handle->dbLock);
    if (fd != handle->fd) {
      entry = hashMap_get(handle->fd_map, (void *) (intptr_t) fd);
      use_handle_fd = true;
    }
    pthread_mutex_unlock(&handle->dbLock);
    if (use_handle_fd) {
      rc = pubsub_tcpHandler_close(handle);
    } else {
      rc = pubsub_tcpHandler_close_connection(handle, entry->url);
    }
  }
  return rc;
}

int pubsub_tcpHandler_listen(pubsub_tcpHandler_pt handle, char *url) {
  handle->fd = pubsub_tcpHandler_open(handle, url);
  int rc = handle->fd ;
  pthread_mutex_lock(&handle->dbLock);
  if (rc >= 0) {
    rc = listen(handle->fd, SOMAXCONN);
    if (rc != 0) {
      fprintf(stderr, "[TCP Socket] Error listen: %s\n", strerror(errno));
      close(handle->fd);
      handle->fd = -1;
    }
  }
  if (rc >=0) {
    int flags = fcntl(handle->fd, F_GETFL, 0);
    if (flags == -1) {
      rc = flags;
    } else {
      rc = fcntl(handle->fd, F_SETFL, flags | O_NONBLOCK);
      if (rc < 0) {
        fprintf(stderr, "[TCP Socket] Cannot set to NON_BLOCKING epoll\n");
        close(handle->fd);
        handle->fd = -1;
      }
    }
  }

  if ((rc >= 0) && (handle->efd >= 0)) {
    struct epoll_event event;
    bzero(&event, sizeof(event)); // zero the struct
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
    event.data.fd = handle->fd;
    rc = epoll_ctl(handle->efd, EPOLL_CTL_ADD, handle->fd, &event);
    if (rc < 0) {
      close(handle->fd);
      handle->fd = -1;
      fprintf(stderr, "[TCP Socket] Cannot create epoll\n");
    }
  }
  pthread_mutex_unlock(&handle->dbLock);
  return  rc;
}


int pubsub_tcpHandler_set_in_addr(const char *hostname, int port, struct sockaddr_in *inp) {
  struct hostent *hp;
  bzero(inp, sizeof(struct sockaddr_in)); // zero the struct
  if (hostname == 0 || hostname[0] == 0) {
    inp->sin_addr.s_addr = INADDR_ANY;
  } else {
    if (!inet_aton(hostname, &inp->sin_addr)) {
      hp = gethostbyname(hostname);
      if (hp == NULL) {
        fprintf(stderr, "[TCP Socket] set_in_addr: Unknown host name %s\n", hostname);
        return -1;
      }
      inp->sin_addr = *(struct in_addr *) hp->h_addr;
    }
  }
  inp->sin_family = AF_INET;
  inp->sin_port = htons(port);
  return 0;
}


static inline
void pubsub_tcpHandler_set_url_info(char *url, pubsub_tcp_url_t *url_info) {
  if (url_info) {
    url_info->protocol = NULL;
    url_info->hostname = NULL;
    url_info->portnr = 0;
  }

  if (url && url_info) {
    url_info->protocol = strtok(strdup(url), "://");
    char *hostname = NULL;
    if (url_info->protocol) {
      hostname = strstr(url, "://");
      if (hostname) {
        hostname += 3;
      }
    } else {
      hostname = url;
    }
    url_info->hostname = strtok(strdup(hostname), ":");
    if (url_info->hostname) {
      char *port = strstr(hostname, ":");
      if (port) {
        port += 1;
        if (isdigit(atoi(port)) == 0) url_info->portnr = atoi(port);
      }
    }
  }
}


int pubsub_tcpHandler_create_receive_buffer_store(pubsub_tcpHandler_pt handle, unsigned int maxNofBuffers,
                                                  unsigned int bufferSize) {
  if (handle != NULL) {
    int i = 0;
    pthread_mutex_lock(&handle->dbLock);
    if (arrayList_create(&handle->bufferLists) != CELIX_SUCCESS) {
      return -1;
    }
    for (i = 0; i < maxNofBuffers; i++) {
      pubsub_tcpBufferPartList_pt item = calloc(1, sizeof(struct pubsub_tcpBufferPartList));
      item->buffer = calloc(sizeof(char), bufferSize);
      item->bufferSize = bufferSize;
      arrayList_add(handle->bufferLists, item);
    }
    pthread_mutex_unlock(&handle->dbLock);
  }
  return 0;
}

void pubsub_tcpHandler_set_timeout(pubsub_tcpHandler_pt handle, unsigned int timeout) {
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    handle->timeout = timeout;
    pthread_mutex_unlock(&handle->dbLock);
  }
}



//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
int  pubsub_tcpHandler_dataAvailable(pubsub_tcpHandler_pt handle, int fd, unsigned int *index, unsigned int *size) {
  pthread_mutex_lock(&handle->dbLock);
  int listSize = arrayList_size(handle->bufferLists);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
  if (!handle->bypassHeader) {

    // Only read the header, we don't know yet where to store the payload
    int nbytes = recv(fd, item->buffer, sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int), MSG_PEEK);
    if (nbytes < 0) {
      fprintf(stderr, "[TCP Socket] read error \n");
      pthread_mutex_unlock(&handle->dbLock);
      return nbytes;
    }
    unsigned int *pBuffer_size = ((unsigned int *) &item->buffer[sizeof(pubsub_tcp_msg_header_t)]);
    unsigned int buffer_size = *pBuffer_size + sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int);
    if (buffer_size > item->bufferSize) {
      free(item->buffer);
      item->buffer = calloc(buffer_size, sizeof(char));
      item->bufferSize = buffer_size;
    }
  }
  int nbytes = recv(fd, item->buffer, item->bufferSize, 0);
  if (nbytes < 0) {
    fprintf(stderr, "[TCP Socket] read error \n");
    pthread_mutex_unlock(&handle->dbLock);
    return nbytes;
  }
  if (!handle->bypassHeader) {
    nbytes-= sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int);
  }

  *index = handle->bufferIdx;
  *size  = nbytes;
  handle->bufferIdx++;
  handle->bufferIdx = handle->bufferIdx % listSize;
  pthread_mutex_unlock(&handle->dbLock);
  return nbytes;
}

//
// Read out the message which is indicated available by the largeUdp_dataAvailable function
//
int
pubsub_tcpHandler_read(pubsub_tcpHandler_pt handle, unsigned int index, pubsub_tcp_msg_header_t **header,
                             void **buffer, unsigned int size) {
  int result = 0;
  pthread_mutex_lock(&handle->dbLock);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, index);
  if (item) {
    if (handle->bypassHeader) {
      *header = &item->default_header;
      *buffer = item->buffer;
      item->default_header.type = (unsigned int) item->buffer[handle->msgIdOffset];
      item->default_header.seqNr = handle->readSeqNr++;
      item->default_header.sendTimeNanoseconds = 0;
      item->default_header.sendTimeNanoseconds = 0;
    } else
    {
      *header = (pubsub_tcp_msg_header_t *) item->buffer;
      *buffer = &item->buffer[sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int)];
    }
  } else {
    result = -1;
  }
  pthread_mutex_unlock(&handle->dbLock);

  return result;
}

int pubsub_tcpHandler_messageHandler(pubsub_tcpHandler_pt handle, void* payload, pubsub_tcpHandler_processMessage_callback_t processMessageCallback){
  int result = 0;
  pthread_mutex_lock(&handle->dbLock);
  handle->processMessageCallback = processMessageCallback;
  handle->processMessagePayload = payload;
  pthread_mutex_unlock(&handle->dbLock);
  return result;
}


//
// Write large data to TCP. .
//
int pubsub_tcpHandler_write(pubsub_tcpHandler_pt handle, pubsub_tcp_msg_header_t *header, void *buffer,
                                  unsigned int size, int flags) {
  pthread_mutex_lock(&handle->dbLock);
  int result = 0;
  int written = 0;

  hash_map_iterator_t iter = hashMapIterator_construct(handle->fd_map);
  while (hashMapIterator_hasNext(&iter)) {
    psa_tcp_connection_entry_t *entry = hashMapIterator_nextValue(&iter);

    // struct iovec *largeMsg_iovec, int len, ,
    struct iovec msg_iovec[MAX_MSG_VECTOR_LEN];
    struct msghdr msg;
    msg.msg_name = &entry->addr;
    msg.msg_namelen = entry->len;
    msg.msg_flags = flags;
    msg.msg_iov = msg_iovec;

    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    if (!handle->bypassHeader) {
      msg.msg_iov[0].iov_base = header;
      msg.msg_iov[0].iov_len = sizeof(*header);
      msg.msg_iov[1].iov_base = &size;
      msg.msg_iov[1].iov_len = sizeof(size);
      msg.msg_iov[2].iov_base = buffer;
      msg.msg_iov[2].iov_len = size;
      msg.msg_iovlen = 3;
    } else {
      msg.msg_iov[0].iov_base = buffer;
      msg.msg_iov[0].iov_len = size;
      msg.msg_iovlen = 1;
    }

    int nbytes = 0;
    if (entry->fd >=0) nbytes = sendmsg(entry->fd, &msg, 0);

    //  Several errors are OK. When speculative write is being done we may not
    //  be able to write a single byte from the socket. Also, SIGSTOP issued
    //  by a debugging tool can result in EINTR error.
    if (nbytes == -1
        && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      result = 0;
      break;
    }
    if (nbytes == -1) {
      fprintf(stderr, "[TCP Socket] Cannot send msg %s\n", strerror(errno));
      result = -1;
      break;
    }
    written += nbytes;
    handle->writeSeqNr++;
  }
  pthread_mutex_unlock(&handle->dbLock);
  return (result == 0 ? written : result);
}

int pubsub_tcpHandler_handler(pubsub_tcpHandler_pt handle) {
  int rc = 0;
  if (handle->efd >= 0) {
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int nof_events = epoll_wait(handle->efd, events, MAX_EPOLL_EVENTS, handle->timeout);
    if (nof_events < 0) {
      close(handle->efd);
      handle->efd = -1;
      fprintf(stderr, "[TCP Socket] Cannot create epoll wait\n");
      return nof_events;
    }
    int i = 0;
    for (i = 0; i < nof_events; i++) {
      if ((handle->fd >= 0) && (events[i].data.fd == handle->fd)) {
        pthread_mutex_lock(&handle->dbLock);
        // new connection available
        struct sockaddr_in their_addr;
        socklen_t len = sizeof(struct sockaddr_in);
        rc = accept(handle->fd, &their_addr, &len);
        if (rc == -1) {
          if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            // already closed
          } else fprintf(stderr, "[TCP Socket] accept failed: %s\n", strerror(errno));
        } else {
          // handle new connection:
          // add it to reactor, etc
          char* address = inet_ntoa(their_addr.sin_addr);
          unsigned int port = their_addr.sin_port;
          char *url = NULL;
          asprintf(&url, "tcp://%s:%u", address, port);
          psa_tcp_connection_entry_t *entry = calloc(1, sizeof(*entry));
          entry->addr = their_addr;
          entry->len = len;
          entry->url = strndup(url, 1024 * 1024);
          entry->fd = rc;
          hashMap_put(handle->fd_map, (void *) (intptr_t) entry->fd, entry);
          fprintf(stdout, "[TCP Socket] New connection to url: %s: \n", url);
        }
        pthread_mutex_unlock(&handle->dbLock);
      } else if (events[i].events & EPOLLIN) {
        int err = 0;
        socklen_t len = sizeof(int);
        rc = getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (rc != 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
          continue;
        }
        unsigned int index = 0;
        unsigned int size = 0;
        rc = pubsub_tcpHandler_dataAvailable(handle, events[i].data.fd, &index, &size);
        if (rc == 0) {
          pubsub_tcpHandler_close_connection_fd(handle, events[i].data.fd);
          continue;
        } else if (rc < 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read error \n");
          continue;
        }
        // Handle data
        pubsub_tcp_msg_header_t *msgHeader = NULL;
        void *buffer = NULL;
        rc = pubsub_tcpHandler_read(handle, index, &msgHeader, &buffer, size);
        if (rc != 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read with index %d\n", index);
          continue;
        }
        pthread_mutex_lock(&handle->dbLock);
        if (handle->processMessageCallback) {
          struct timespec receiveTime;
          clock_gettime(CLOCK_REALTIME, &receiveTime);
          handle->processMessageCallback(handle->processMessagePayload, msgHeader, buffer, size, &receiveTime);
         }
        pthread_mutex_unlock(&handle->dbLock);
      } else if (events[i].events & EPOLLOUT)  {
        int err = 0;
        socklen_t len = sizeof(int);
        rc = getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (rc != 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
          continue;
        }
      } else if (events[i].events & EPOLLRDHUP)  {
        int err = 0;
        socklen_t len = sizeof(int);
        rc = getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (rc != 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
          continue;
        }
        pubsub_tcpHandler_close_connection_fd(handle, events[i].data.fd);
      } else if (events[i].events & EPOLLERR) {
        fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
        continue;
      }
    }
  }
  return rc;
}