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
#include "pubsub_tcp_buffer_handler.h"

#define IP_HEADER_SIZE  20
#define TCP_HEADER_SIZE 20
#define MAX_EPOLL_EVENTS        16
#define RECV_THREAD_TIMEOUT     (5 * 1000)
#define MAX_MSG_VECTOR_LEN 256

typedef bool (*pubsub_tcpBufferHandler_dataAvailable_callback_t)(pubsub_tcpBufferHandler_pt handle, int fd,
                                                                 unsigned int *index, unsigned int *size);

struct pubsub_tcpBufferHandler {
  array_list_pt bufferLists;
  unsigned int bufferIdx;
  unsigned int readSeqNr;
  unsigned int writeSeqNr;
  unsigned int msgIdOffset;
  unsigned int mtuSize;
  unsigned int mtuBufferSize;
  bool bypassIpFragmentation;
  bool bypassHeader;
  pthread_mutex_t dbLock;
  pubsub_tcpBufferHandler_dataAvailable_callback_t dataAvailableCallback;
  pubsub_tcpBufferHandler_processMessage_callback_t processMessageCallback;
  void *processMessagePayload;
  hash_map_t *rx_url_map;
  hash_map_t *rx_fd_map;
  hash_map_t *tx_fd_map;
  bool connected;
  int efd;
  int fd;
};

typedef struct pubsub_tcpBufferPartList {
  unsigned int msgSeqNr;
  unsigned int msgSize;
  unsigned int nrPartsRemaining;
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
  pubsub_tcpBufferHandler_connectMessage_callback_t disconnectMessageCallback;
  void* payload;
} psa_tcp_connection_entry_t;


typedef struct pubsub_tcp_seq_msg_header {
  unsigned int msg_sequence_number;
  unsigned int total_msg_size;
  unsigned int part_msg_size;
  unsigned int offset;
} pubsub_tcp_seq_msg_header_t;


static inline bool
pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index,
                                                        unsigned int *size);


static inline bool
pubsub_tcpBufferHandler_dataAvailable_full_message(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index,
                                             unsigned int *size);


static inline bool
pubsub_tcpBufferHandler_dataAvailable_msg_id(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index,
                                             unsigned int *size);

static inline int pubsub_tcpBufferHandler_set_in_addr(const char *hostname, int port, struct sockaddr_in *inp);

static inline void pubsub_tcpBufferHandler_set_url_info(char *url, pubsub_tcp_url_t *url_info);


//
// Create a handle
//
pubsub_tcpBufferHandler_pt pubsub_tcpBufferHandler_create() {
  pubsub_tcpBufferHandler_pt handle = calloc(sizeof(*handle), 1);
  if (handle != NULL) {
    handle->fd = -1;
    handle->efd = epoll_create1(0);
    handle->bufferIdx = 0;
    handle->dataAvailableCallback = NULL;
    handle->rx_url_map = hashMap_create(NULL, NULL, NULL, NULL);
    handle->rx_fd_map = hashMap_create(NULL, NULL, NULL, NULL);
    handle->tx_fd_map = hashMap_create(NULL, NULL, NULL, NULL);
    handle->mtuBufferSize = 64435 * 2;
    pthread_mutex_init(&handle->dbLock, 0);
    signal(SIGPIPE, SIG_IGN);
  }
  return handle;
}

int pubsub_tcpBufferHandler_create_receive_buffer(pubsub_tcpBufferHandler_pt handle, unsigned int maxNofBuffers,
                                                  unsigned int bufferSize, bool bypassIpFragmentation) {
  if (handle != NULL) {
    int i = 0;
    pthread_mutex_lock(&handle->dbLock);
    handle->bypassIpFragmentation = true;//bypassIpFragmentation;

    handle->dataAvailableCallback =  &pubsub_tcpBufferHandler_dataAvailable_full_message;
    //handle->dataAvailableCallback = (handle->bypassIpFragmentation) ? &pubsub_tcpBufferHandler_dataAvailable_msg_id
    //                                                                : &pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation;
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

//
// Destroys the handle
//
void pubsub_tcpBufferHandler_destroy(pubsub_tcpBufferHandler_pt handle) {
  printf("### Destroying BufferHAndler TCP\n");
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    pubsub_tcpBufferHandler_close(handle);
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
    hashMap_destroy(handle->rx_url_map, false, false);
    hashMap_destroy(handle->rx_fd_map, false, false);
    hashMap_destroy(handle->tx_fd_map, false, false);
    pubsub_tcpBufferHandler_close(handle);
    if (handle->efd >= 0) close(handle->efd);
    handle->bufferLists = NULL;
    pthread_mutex_unlock(&handle->dbLock);
    pthread_mutex_destroy(&handle->dbLock);
    free(handle);
  }
}

// Destroys the handle
//
int pubsub_tcpBufferHandler_open(pubsub_tcpBufferHandler_pt handle, char *url) {
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
    pubsub_tcpBufferHandler_set_url_info(url, &url_info);
    rc = pubsub_tcpBufferHandler_set_in_addr(url_info.hostname, url_info.portnr, &addr);
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
int pubsub_tcpBufferHandler_close(pubsub_tcpBufferHandler_pt handle) {
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

int pubsub_tcpBufferHandler_connect(pubsub_tcpBufferHandler_pt handle, char *url, void* payload, pubsub_tcpBufferHandler_connectMessage_callback_t connectMessageCallback, pubsub_tcpBufferHandler_connectMessage_callback_t disconnectMessageCallback) {
  int fd = pubsub_tcpBufferHandler_open(handle, NULL);
  int rc = fd;
  pthread_mutex_lock(&handle->dbLock);
  pubsub_tcp_url_t url_info;
  psa_tcp_connection_entry_t *entry = NULL;
  struct sockaddr_in addr; // connector's address information
  if (rc >= 0) {
    pubsub_tcpBufferHandler_set_url_info(url, &url_info);
    rc = pubsub_tcpBufferHandler_set_in_addr(url_info.hostname, url_info.portnr, &addr);
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
    hashMap_put(handle->rx_url_map, entry->url, entry);
    hashMap_put(handle->rx_fd_map, (void *) (intptr_t) entry->fd, entry);
  }
  free(url_info.hostname);
  free(url_info.protocol);
  pthread_mutex_unlock(&handle->dbLock);
  return rc;
}

// Destroys the handle
//
int pubsub_tcpBufferHandler_close_connection(pubsub_tcpBufferHandler_pt handle, char *url) {
  int rc = 0;
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    psa_tcp_connection_entry_t *entry = hashMap_remove(handle->rx_url_map, url);
    hashMap_remove(handle->rx_fd_map, (void *) (intptr_t) entry->fd);
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
int pubsub_tcpBufferHandler_close_connection_fd(pubsub_tcpBufferHandler_pt handle, int fd) {
  int rc = 0;
  if (handle != NULL) {
    bool use_handle_fd = false;
    psa_tcp_connection_entry_t *entry = NULL;
    pthread_mutex_lock(&handle->dbLock);
    if (fd != handle->fd) {
      entry = hashMap_get(handle->rx_fd_map, (void *) (intptr_t) fd);
      use_handle_fd = true;
    }
    pthread_mutex_unlock(&handle->dbLock);
    if (use_handle_fd) {
      rc = pubsub_tcpBufferHandler_close(handle);
    } else {
      rc = pubsub_tcpBufferHandler_close_connection(handle, entry->url);
    }
  }
  return rc;
}

int pubsub_tcpBufferHandler_listen(pubsub_tcpBufferHandler_pt handle, char *url) {
  handle->fd = pubsub_tcpBufferHandler_open(handle, url);
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


int pubsub_tcpBufferHandler_set_in_addr(const char *hostname, int port, struct sockaddr_in *inp) {
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
void pubsub_tcpBufferHandler_set_url_info(char *url, pubsub_tcp_url_t *url_info) {
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

//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
static inline bool pubsub_tcpBufferHandler_dataAvailable_full_message(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size) {
  int listSize = arrayList_size(handle->bufferLists);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
  int result = false;
  // Only read the header, we don't know yet where to store the payload
  if (recv(fd, item->buffer, sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int), MSG_PEEK) < 0) {
    fprintf(stderr, "[TCP Socket] read error \n");
    return result;
  }

  unsigned int* pBuffer_size = ((unsigned int*)&item->buffer[sizeof(pubsub_tcp_msg_header_t)]);
  unsigned int buffer_size = *pBuffer_size;
  pubsub_tcp_msg_header_t* hdr = (pubsub_tcp_msg_header_t*)item->buffer;
  if (hdr) *size  = buffer_size;


  if (recv(fd, item->buffer, buffer_size + sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int), 0) < 0) {
    fprintf(stderr, "[TCP Socket] read error \n");
    return result;
  }
  pubsub_tcp_msg_header_t* hdr2 = (pubsub_tcp_msg_header_t*)&item->buffer[sizeof(pubsub_tcp_msg_header_t) + sizeof(unsigned int)];
  if (hdr2) *size  = buffer_size;

  *index = handle->bufferIdx;
  *size  = buffer_size;
  handle->bufferIdx++;
  handle->bufferIdx = handle->bufferIdx % listSize;
  return true;
}



//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
static inline bool pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation(pubsub_tcpBufferHandler_pt handle, int fd,
                                                                           unsigned int *index, unsigned int *size) {
  pubsub_tcp_seq_msg_header_t header;
  int result = false;
  // Only read the header, we don't know yet where to store the payload
  if (recv(fd, &header, sizeof(header), MSG_PEEK) < 0) {
    fprintf(stderr, "[TCP Socket] read error \n");
    return result;
  }

  struct iovec msg_vec[2];
  struct msghdr msg;
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_flags = 0;
  msg.msg_iov = msg_vec;
  msg.msg_iovlen = 2; // header and payload;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;

  msg.msg_iov[0].iov_base = &header;
  msg.msg_iov[0].iov_len = sizeof(header);

  int i = 0;
  int listSize = arrayList_size(handle->bufferLists);
  bool found = false;
  for (i = 0; i < listSize; i++) {
    pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, i);
    if (item->msgSeqNr == header.msg_sequence_number) {
      found = true;
      msg.msg_iov[1].iov_base = &item->buffer[header.offset];
      msg.msg_iov[1].iov_len = header.part_msg_size;
      if (recvmsg(fd, &msg, 0) < 0) {
        result = false;
        break;
      }

      item->nrPartsRemaining--;
      if (item->nrPartsRemaining == 0) {
        *index = i;
        if (handle->bypassHeader) {
          *size = item->msgSize;
        } else {
          *size = item->msgSize - sizeof(pubsub_tcp_msg_header_t);
        }
        result = true;
        break;
      } else {
        result = false; // not complete
        break;
      }
    }
  }

  if ((found == false) && (result == true)) {
    pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
    if (item->nrPartsRemaining) {
      fprintf(stderr, "ERROR: Removing entry[%d] for id %d: %d parts not received\n", handle->bufferIdx,
              item->msgSeqNr, item->nrPartsRemaining);
    }
    item->msgSize = header.total_msg_size;
    item->msgSeqNr = header.msg_sequence_number;
    item->nrPartsRemaining = item->msgSize / handle->mtuBufferSize;
    if (item->msgSize > item->bufferSize) {
      free(item->buffer);
      item->buffer = calloc(sizeof(char), header.total_msg_size);
    }

    msg.msg_iov[1].iov_base = &item->buffer[header.offset];
    msg.msg_iov[1].iov_len = header.part_msg_size;
    if (recvmsg(fd, &msg, 0) < 0) {
      result = false;
    } else {
      if (item->nrPartsRemaining == 0) {
        *index = handle->bufferIdx;
        if (handle->bypassHeader) {
          *size = item->msgSize;
        } else {
          *size = item->msgSize - sizeof(pubsub_tcp_msg_header_t);
        }
        result = true;
      } else {
        result = false;
      }
    }
    handle->bufferIdx = handle->bufferIdx + 1 % listSize;
  }
  return result;
}


//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
static inline bool
pubsub_tcpBufferHandler_dataAvailable_msg_id(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index,
                                             unsigned int *size) {

  int listSize = arrayList_size(handle->bufferLists);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
  if (!item) return false;
  // Only read the header, we don't know yet where to store the payload
  int readSize = recv(fd, item->buffer, item->bufferSize, MSG_PEEK);
  if (size < 0) {
    fprintf(stderr, "[TCP Socket] Read Error: %s\n", strerror(errno));
    return false;
  }
  *index = handle->bufferIdx;
  if (handle->bypassHeader) {
    *size = readSize;
  } else {
    *size = readSize - sizeof(pubsub_tcp_msg_header_t);
  }
  handle->bufferIdx++;
  handle->bufferIdx = handle->bufferIdx % listSize;
  return true;
}

//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
bool pubsub_tcpBufferHandler_dataAvailable(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index,
                                           unsigned int *size) {
  pthread_mutex_lock(&handle->dbLock);
  int result = false;
  if (handle->dataAvailableCallback) {
    result = handle->dataAvailableCallback(handle, fd, index, size);
  }
  pthread_mutex_unlock(&handle->dbLock);
  return result;
}

//
// Read out the message which is indicated available by the largeUdp_dataAvailable function
//
int
pubsub_tcpBufferHandler_read(pubsub_tcpBufferHandler_pt handle, unsigned int index, pubsub_tcp_msg_header_t **header,
                             void **buffer, unsigned int size) {
  int result = 0;
  pthread_mutex_lock(&handle->dbLock);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, index);
  if (item) {
#if NOT
    if (handle->bypassHeader) {
      *header = &item->default_header;
      *buffer = item->buffer;
      item->default_header.type = (unsigned int) item->buffer[handle->msgIdOffset];
      item->default_header.seqNr = handle->readSeqNr++;
      item->default_header.sendTimeNanoseconds = 0;
      item->default_header.sendTimeNanoseconds = 0;
    } else
#endif
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

int pubsub_tcpBufferHandler_messageHandler(pubsub_tcpBufferHandler_pt handle, void* payload, pubsub_tcpBufferHandler_processMessage_callback_t processMessageCallback){
  int result = 0;
  pthread_mutex_lock(&handle->dbLock);
  handle->processMessageCallback = processMessageCallback;
  handle->processMessagePayload = payload;
  pthread_mutex_unlock(&handle->dbLock);
  return result;
}


//
// Write large data to UDP. This function splits the data in chunks and sends these chunks with a header over UDP.
//
int pubsub_tcpBufferHandler_write(pubsub_tcpBufferHandler_pt handle, pubsub_tcp_msg_header_t *header, void *buffer,
                                  unsigned int size, int flags) {
  pthread_mutex_lock(&handle->dbLock);
  int result = 0;
  int written = 0;

  //fprintf(stdout, "header %d, %d, %d,%d\n", header->type, header->seqNr, header->major, header->minor);

  hash_map_iterator_t iter = hashMapIterator_construct(handle->tx_fd_map);
  while (hashMapIterator_hasNext(&iter)) {
    psa_tcp_connection_entry_t *entry = hashMapIterator_nextValue(&iter);

    // struct iovec *largeMsg_iovec, int len, ,
    int n;
    const int iovec_len = 3; // header + size + payload
    struct iovec large_msg_iovec[iovec_len];
    large_msg_iovec[0].iov_base = header;
    large_msg_iovec[0].iov_len = sizeof(*header);
    large_msg_iovec[1].iov_base = &size;
    large_msg_iovec[1].iov_len = sizeof(size);
    large_msg_iovec[2].iov_base = buffer;
    large_msg_iovec[2].iov_len = size;
    pubsub_tcp_seq_msg_header_t seq_header;

    seq_header.msg_sequence_number = handle->writeSeqNr;
    seq_header.total_msg_size = 0;
    for (n = 0; n < iovec_len; n++) {
      seq_header.total_msg_size += large_msg_iovec[n].iov_len;
    }
    //int nr_buffers = (handle->mtuBufferSize) ? (seq_header.total_msg_size / handle->mtuBufferSize) + 1 : 1;
    //nr_buffers = 1;
    struct iovec msg_iovec[MAX_MSG_VECTOR_LEN];
    struct msghdr msg;
    msg.msg_name = &entry->addr;
    msg.msg_namelen = entry->len;
    msg.msg_flags = flags;
    msg.msg_iov = msg_iovec;
    //msg.msg_iovlen = 2; // header and payload;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;


    msg.msg_iov[0].iov_base = header;
    msg.msg_iov[0].iov_len = sizeof(*header);
    msg.msg_iov[1].iov_base = &size;
    msg.msg_iov[1].iov_len = sizeof(size);
    msg.msg_iov[2].iov_base = buffer;
    msg.msg_iov[2].iov_len = size;

    //msg.msg_iov[0].iov_base = &seq_header;
    //msg.msg_iov[0].iov_len = sizeof(seq_header);

    msg.msg_iovlen = 3;
    //msg.msg_iov[0].iov_base = &seq_header;
    //msg.msg_iov[0].iov_len = sizeof(seq_header);

#if NOT
    for (n = 0; n < nr_buffers; n++) {

      seq_header.part_msg_size = (((seq_header.total_msg_size - n * handle->mtuBufferSize) > handle->mtuBufferSize)
                                  ? handle->mtuBufferSize : (seq_header.total_msg_size - n * handle->mtuBufferSize));
      seq_header.offset = n * handle->mtuBufferSize;
      int remainingOffset = seq_header.offset;
      int recvPart = 0;
      // find the start of the part
      while (remainingOffset > large_msg_iovec[recvPart].iov_len) {
        remainingOffset -= large_msg_iovec[recvPart].iov_len;
        recvPart++;
      }
      msg.msg_iovlen = 1;
//#endif
//#if NOT
      int remainingData = seq_header.part_msg_size;
      int sendPart = 1;
      // fill in the output iovec from the input iovec in such a way that all UDP frames are filled maximal.
      while (remainingData > 0) {
        int partLen = ((large_msg_iovec[recvPart].iov_len - remainingOffset) <= remainingData ? (
                large_msg_iovec[recvPart].iov_len - remainingOffset) : remainingData);
        msg.msg_iov[sendPart].iov_base = large_msg_iovec[recvPart].iov_base + remainingOffset;
        msg.msg_iov[sendPart].iov_len = partLen;
        remainingData -= partLen;
        remainingOffset = 0;
        sendPart++;
        recvPart++;
        msg.msg_iovlen++;
      }
      int tmp, tmptot;
      for (tmp = 0, tmptot = 0; tmp < msg.msg_iovlen; tmp++) {
        tmptot += msg.msg_iov[tmp].iov_len;
      }
#endif
      int nbytes = 0;
      if (entry->fd >=0) nbytes = sendmsg(entry->fd, &msg, 0);
      //if (entry->fd >=0) nbytes = send(entry->fd, buffer, size, flags);
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
    //}
    handle->writeSeqNr++;
  }
  pthread_mutex_unlock(&handle->dbLock);
  return (result == 0 ? written : result);
}

int pubsub_tcpBufferHandler_handler(pubsub_tcpBufferHandler_pt handle) {
  int rc = 0;
  if (handle->efd >= 0) {
    struct epoll_event events[MAX_EPOLL_EVENTS];
    int nof_events = epoll_wait(handle->efd, events, MAX_EPOLL_EVENTS, RECV_THREAD_TIMEOUT);
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
          hashMap_put(handle->tx_fd_map, (void *) (intptr_t) entry->fd, entry);
          fprintf(stdout, "[TCP Socket] New connection to url: %s: \n", url);
          if (handle->bypassIpFragmentation == false)  { // false
#if NOT
            socklen_t mtuSizeLen = sizeof(int);
            rc = getsockopt(handle->fd, IPPROTO_IP, IP_MTU, &handle->mtuSize, &mtuSizeLen);
            handle->mtuBufferSize =
                    handle->mtuSize - IP_HEADER_SIZE - TCP_HEADER_SIZE - sizeof(pubsub_tcp_seq_msg_header_t);
            if (rc != 0) {
              fprintf(stderr, "[TCP Socket] Error getsockopt(IP_MTU): %s\n", strerror(errno));
            }
#endif
            if (rc == 0) {
              rc = setsockopt(handle->fd, SOL_SOCKET, SO_SNDBUF,
                              &handle->mtuBufferSize + sizeof(pubsub_tcp_seq_msg_header_t),
                              sizeof(handle->mtuBufferSize));
              if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_SNDBUF): %s\n", strerror(errno));
              rc = setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF,
                              &handle->mtuBufferSize + sizeof(pubsub_tcp_seq_msg_header_t),
                              sizeof(handle->mtuBufferSize));
              if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_RCVBUF): %s\n", strerror(errno));
            }
          }
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
        rc = pubsub_tcpBufferHandler_dataAvailable(handle, events[i].data.fd, &index, &size);
        if (rc == 0) {
          pubsub_tcpBufferHandler_close_connection_fd(handle, events[i].data.fd);
          continue;
        } else if (rc < 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read error \n");
          continue;
        }
        // Handle data
        pubsub_tcp_msg_header_t *msgHeader = NULL;
        void *buffer = NULL;
        rc = pubsub_tcpBufferHandler_read(handle, index, &msgHeader, &buffer, size);
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
      } else if ((events[i].events & EPOLLOUT) || (events[i].events & EPOLLRDHUP))  {
        int err = 0;
        socklen_t len = sizeof(int);
        rc = getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (rc != 0) {
          fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
          continue;
        }
        pubsub_tcpBufferHandler_close_connection_fd(handle, events[i].data.fd);
      } else if (events[i].events & EPOLLERR) {
        fprintf(stderr, "[TCP Socket]: ERROR read from socket \n");
        continue;
      }
    }
  }
  return rc;
}