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
#include <arpa/inet.h>

#define IP_HEADER_SIZE  20
#define TCP_HEADER_SIZE 20
#define MAX_EPOLL_EVENTS        16
#define RECV_THREAD_TIMEOUT     (5 * 1000)
#define MAX_MSG_VECTOR_LEN 256

typedef bool (*pubsub_tcpBufferHandler_dataAvailable_callback_t)(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size);
//typedef struct pubsub_tcpBufferHandler  *pubsub_tcpBufferHandler_pt;

struct pubsub_tcpBufferHandler {
  array_list_pt bufferLists;
  unsigned int bufferIdx;
  unsigned int readSeqNr;
  unsigned int writeSeqNr;
  unsigned int msgIdOffset;
  unsigned int mtuSize;
  unsigned int mtuBufferSize;
  bool bypassIpFragmentation
  bool bypassHeader;
  pthread_mutex_t dbLock;
  pubsub_tcpBufferHandler_dataAvailable_callback_t dataAvailableCallback;
  pubsub_tcpBufferHandler_processMessage_callback_t processMessageCallback;
  void* processMessagePayload;
  int fd;
  int efd;
  bool connected;
};

typedef struct pubsub_tcpBufferPartList {
  unsigned int msgSeqNr;
  unsigned int msgSize;
  unsigned int nrPartsRemaining;
  pubsub_tcp_msg_header_t default_header;
  unsigned int bufferSize;
  char *buffer;
} *pubsub_tcpBufferPartList_pt;


typedef struct pubsub_tcp_seq_msg_header {
  unsigned int msg_sequence_number;
  unsigned int total_msg_size;
  unsigned int part_msg_size;
  unsigned int offset;
} pubsub_tcp_seq_msg_header_t;


static inline bool pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size);
static inline bool pubsub_tcpBufferHandler_dataAvailable_msg_id(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size);

//
// Create a handle
//
pubsub_tcpBufferHandler_pt pubsub_tcpBufferHandler_create(unsigned int maxNofBuffers, unsigned int bufferSize, bool bypassIpFragmentation) {
  printf("## Creating large TCP Buffers \n");
  int i = 0;
  pubsub_tcpBufferHandler_pt handle = calloc(sizeof(*handle), 1);
  if (handle != NULL) {
    handle->fd = -1;
    handle->efd = -1;
    handle->connected = false;
    handle->buffer_idx = 0;
    handle->bypassIpFragmentation = bypassIpFragmentation;
    handle->dataAvailableCallback = (bypassIpFragmentation) ? &pubsub_tcpBufferHandler_dataAvailable_msg_id : &pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation;
    if (arrayList_create(&handle->bufferLists) != CELIX_SUCCESS) {
      free(handle);
      handle = NULL;
      return  NULL;
    }
    for (i = 0; i < maxNofBuffers;i++) {
      pubsub_tcpBufferPartList_pt item = calloc(sizeof(*item), 1);
      item->buffer = calloc(sizeof(char), bufferSize);
      item->buffer_size = bufferSize;
      arrayList_add(handle->bufferLists, item);
    }
    pthread_mutex_init(&handle->dbLock, 0);
  }
  return handle;
}

//
// Destroys the handle
//
void pubsub_tcpBufferHandler_destroy(pubsub_tcpBufferHandler_pt handle) {
  printf("### Destroying large TCP\n");
  if (handle != NULL) {
    pthread_mutex_lock(&handle->dbLock);
    if (handle->efd >=0) {
      struct epoll_event event;
      bzero(&event, sizeof(epoll_event)); // zero the struct
      event.data.fd = handle->fd; // user data
      epoll_ctl(handle->efd, EPOLL_CTL_DEL, handle->fd, &event);
      handle->connected = false;
      close(handle->efd);
      handle->efd = -1;
    }
    if (handle->fd) {
      close(handle->fd)
      handle->fd = -1;
    }

    int listSize = arrayList_size(handle->bufferLists);
    int i;
    for (i = 0; i < listSize; i++) {
      pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
      if (item) {
        if (item->buffer) {
          free(item->buffer);
          item->buffer = NULL;
        }
        free(item);
      }
    }
    arrayList_destroy(handle->bufferLists);
    handle->bufferLists = NULL;
    pthread_mutex_unlock(&handle->dbLock);
    pthread_mutex_destroy(&handle->dbLock);
    free(handle);
  }
}

// Destroys the handle
//
static inline int pubsub_tcpBufferHandler_get_socket(pubsub_tcpBufferHandler_pt handle) {
  handle->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK);
  int rc = handle->fd;
  if (handle->fd >= 0) {
    int setting = 1;
    if (rc == 0) {
      rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &setting, sizeof(setting));
      if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_REUSEADDR): %s", strerror(errno));
    }
    if (rc == 0) {
      rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &setting, sizeof(setting));
      if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(TCP_NODELAY): %s", strerror(errno));
    }
  } else {
    fprintf(stderr, "[TCP Socket] Error creating socket: %s", strerror(errno));
  }
  return rc;
}


static inline int pubsub_tcpBufferHandler_set_in_addr(const char *hostname, int port, struct sockaddr_in *inp) {
  struct hostent *hp;
  bzero(&addr, sizeof(sockaddr_in)); // zero the struct
  if ( hostname == 0 || hostname[0] == 0  ){
    inp->sin_addr.s_addr = INADDR_ANY;
  }
  else{
    if (!inet_aton( hostname, &inp->sin_addr ) ){
      hp = gethostbyname( hostname );
      if ( hp == NULL ){
        fprintf(stderr, "[TCP Socket] set_in_addr: Unknown host name %s\n", hostname);
        return -1;
      }
      inp->sin_addr = *(struct in_addr*)hp->h_addr;
    }
  }
  inp->sin_family     = AF_INET;
  inp->sin_port       = htons(port);
  return 0;
}


static inline int pubsub_tcpBufferHandler_connect(pubsub_tcpBufferHandler_pt handle, const char *hostname, int port) {
  int connect_result = 0;
  handle->connected = 1;
  struct sockaddr_in addr; // connector's address information
  int rc = pubsub_tcp_socket_set_in_addr(hostname, port, &addr);
  if (rc == 0) connect_result = connect(handle->fd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
  if (handle->fd >= 0 && rc >= 0 && handle->efd== -1) {
    handle->efd = epoll_create1(0);
    struct epoll_event event;
    bzero(&event, sizeof(epoll_event)); // zero the struct
    event.events = EPOLLIN;
    if ((connect_result < 0) && (errno == EINPROGRESS)) {
      event.events |= EPOLLOUT;
      handle->connected = 0;
    } else if (handle->bypassIpFragmentation == false) {
      int rc = getsockopt(handle->fd, IPPROTO_IP, IP_MTU, &handle->mtuSize, &len);
      handle->mtuBufferSize = handle->mtuSize - IP_HEADER_SIZE - TCP_HEADER_SIZE - sizeof(pubsub_tcp_seq_msg_header_t);
      if (rc != 0) fprintf(stderr, "[TCP Socket] Error getsockopt(IP_MTU): %s", strerror(errno));
      if (rc == 0) {
        rc = setsockopt(handle->fd, SOL_SOCKET, SO_SNDBUF, &handle->mtuBufferSize,
                        sizeof(handle->mtuBufferSize));
        if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_SNDBUF): %s", strerror(errno));
        rc = setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF, &handle->mtuBufferSize,
                        sizeof(handle->mtuBufferSize));
        if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_RCVBUF): %s", strerror(errno));
      }
    }
    event.data.fd = handle->fd;
    rc = epoll_ctl(handle->efd, EPOLL_CTL_ADD, handle->fd, &event);
    if (rc < 0) {
      close(handle->efd);
      handle->efd = -1;
      fprintf(stderr, "[TCP Socket] Cannot connect to %s\n", hostname);
    }
  }
  return rc;
}

static inline int pubsub_tcpBufferHandler_bind(pubsub_tcpBufferHandler_pt handle, const char *hostname, int port) {
  struct sockaddr_in addr; // connector's address information
  int rc = pubsub_tcp_socket_set_in_addr(hostname, port, &addr);
  if (rc == 0) rc = bind(handle->fd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
  if (rc != 0) fprintf(stderr, "[TCP Socket] Error bind: %s", strerror(errno));
  if (rc == 0) {rc = listen(handle->fd, 1);
  if (rc != 0) fprintf(stderr, "[TCP Socket] Error listen: %s", strerror(errno));

  if (handle->fd >= 0 && rc >= 0 && handle->efd== -1) {
    handle->efd = epoll_create1(0);
    struct epoll_event event;
    bzero(&event, sizeof(epoll_event)); // zero the struct
    event.events = EPOLLIN;
    if (handle->bypassIpFragmentation == false) {
      int rc = getsockopt(handle->fd, IPPROTO_IP, IP_MTU, &handle->mtuSize, &len);
      handle->mtuBufferSize =
              handle->mtuSize - IP_HEADER_SIZE - TCP_HEADER_SIZE - sizeof(pubsub_tcp_seq_msg_header_t);
      if (rc != 0) fprintf(stderr, "[TCP Socket] Error getsockopt(IP_MTU): %s", strerror(errno));
      if (rc == 0) {
        rc = setsockopt(handle->fd, SOL_SOCKET, SO_SNDBUF, &handle->mtuBufferSize,
                        sizeof(handle->mtuBufferSize));
        if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_SNDBUF): %s", strerror(errno));
        rc = setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF, &handle->mtuBufferSize,
                        sizeof(handle->mtuBufferSize));
        if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_RCVBUF): %s", strerror(errno));
      }
      event.data.fd = handle->fd;
      rc = epoll_ctl(handle->efd, EPOLL_CTL_ADD, handle->fd, &event);
      if (rc < 0) {
        close(handle->efd);
        handle->efd = -1;
        fprintf(stderr, "[TCP Socket] Cannot bind to %s\n", hostname);
      }
    }
  }
  return rc;
}

//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
static inline bool pubsub_tcpBufferHandler_dataAvailable_msg_fragmentation(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size) {
  pubsub_tcp_seq_msg_header_t header;
  int result = false;
  // Only read the header, we don't know yet where to store the payload
  if (recv(fd, header, sizeof(header), MSG_PEEK) < 0) {
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
  bool found = true;
  for(i = 0; i < listSize; i++) {
    pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, i);
    if (item->msgSeqNr == header.msg_sequence_number) {
      found = true;
      msg.msg_iov[1].iov_base = &item->data[header.offset];
      msg.msg_iov[1].iov_len = header.part_msg_size;
      if (recvmsg(fd, &msg, 0) < 0) {
        result = false;
        break;
      }

      item->nrPartsRemaining--;
      if (item->nrPartsRemaining == 0) {
        *index = i;
        if (handle->bypassHeader) {
          *size  = item->msg_size;
        } else {
          *size  = item->msg_size - sizeof(pubsub_tcp_msg_header_t);
        }
        result = true;
        break;
      } else {
        result = false; // not complete
        break;
      }
    }
  }
  if (!found && result) {
    pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
    if (item->nrPartsRemaining) {
      fprintf(stderr, "ERROR: Removing entry[%d] for id %d: %d parts not received\n", handle->bufferIdx, item->msg_ident, item->nrPartsRemaining);
    }
    item->msg_ident = header.msg_ident;
    item->msg_size  = header.total_msg_size;
    item->msgSeqNr  = header.msg_sequence_number;
    item->nrPartsRemaining = item->msg_size / handle->mtuBufferSize;
    if (item->msg_size  > item->bufferSize) {
      free(item->data);
      item->data = calloc(sizeof(char), header.total_msg_size);
    }

    msg.msg_iov[1].iov_base = &item->data[header.offset];
    msg.msg_iov[1].iov_len  = header.part_msg_size;
    if(recvmsg(fd, &msg, 0) < 0){
      result = false;
    }
    else {
      if(item->nrPartsRemaining == 0) {
        *index = handle->bufferIdx;
        if (handle->bypassHeader) {
          *size  = item->msg_size;
        } else {
          *size  = item->msg_size - sizeof(pubsub_tcp_msg_header_t);
        }
        result = true;
      } else {
        result = false;
      }
    }
    handle->bufferIdx++ % listSize;
  }
  return result;
}


//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
static inline bool pubsub_tcpBufferHandler_dataAvailable_msg_id(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size) {

  int listSize = arrayList_size(handle->bufferLists);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, handle->bufferIdx);
  // Only read the header, we don't know yet where to store the payload
  int readSize = recv(fd, item->buffer, item->bufferSize, MSG_PEEK);
  if (size < 0) {
    fprintf(stderr, "[TCP Socket] Read Error: %s\n", strerror(errno));
    return false;
  }
  *index = handle->bufferIdx;
  if (handle->bypassHeader) {
    *size  = readSize;
  } else {
    *size  = readSize - sizeof(pubsub_tcp_msg_header_t);
  }
  handle->bufferIdx++ % listSize;
  return true;
}

//
// Reads data from the filedescriptor which has date (determined by epoll()) and stores it in the internal structure
// If the message is completely reassembled true is returned and the index and size have valid values
//
int pubsub_tcpBufferHandler_dataAvailable(pubsub_tcpBufferHandler_pt handle, int fd, unsigned int *index, unsigned int *size) {
  pthread_mutex_lock(&handle->dbLock);
  int result = false;
  if (handle->dataAvailableCallback){
    result = handle->dataAvailableCallback(handle, fd, index, size);
  }
  pthread_mutex_unlock(&handle->dbLock);
  return result;
}

//
// Read out the message which is indicated available by the largeUdp_dataAvailable function
//
int pubsub_tcpBufferHandler_read(pubsub_tcpBufferHandler_pt handle, unsigned int index, pubsub_tcp_msg_header_t** header, void ** buffer, unsigned int size)
{
  int result = 0;
  pthread_mutex_lock(&handle->dbLock);
  pubsub_tcpBufferPartList_pt item = arrayList_get(handle->bufferLists, index);
  if(item) {
    if (handle->bypassHeader) {
      *header = &item->default_header;
      *buffer = item->data;
      item->default_header.type  = (unsigned int) &item->data[handle->msg_id_offset];
      item->default_header.seqNr = handle->readSeqNr++;
      item->default_header.sendTimeNanoseconds = 0;
      item->default_header.sendTimeNanoseconds = 0;
    } else {
      *header = item->data;
      *buffer = &item->data[sizeof(pubsub_tcp_msg_header_t)];
    }
  } else {
    result = -1;
  }
  pthread_mutex_unlock(&handle->dbLock);

  return result;
}


  //
// Write large data to UDP. This function splits the data in chunks and sends these chunks with a header over UDP.
//
int pubsub_tcpBufferHandler_write(largeUdp_pt handle, pubsub_tcp_msg_header_t* header, void* buffer, unsigned int size, int flags,  struct sockaddr_in *dest_addr, size_t addrlen){

  // struct iovec *largeMsg_iovec, int len, ,
  int n;
  int result = 0;
  bool ret = true;

  const int iovec_len = 3; // header + size + payload
  struct iovec large_msg_iovec[iovec_len];
  large_msg_iovec[0].iov_base = header;
  large_msg_iovec[0].iov_len = sizeof(*header);
  large_msg_iovec[1].iov_base = &size;
  large_msg_iovec[1].iov_len = sizeof(size);
  large_msg_iovec[2].iov_base = buffer;
  large_msg_iovec[2].iov_len = size;

  pubsub_tcp_seq_msg_header seq_header;

  int written = 0;
  seq_header.msg_sequence_number = handle->writeSeqNr;
  seq_header.total_msg_size = 0;
  for(n = 0; n < iovec_len ;n++) {
    seq_header.total_msg_size += large_msg_iovec[n].iov_len;
  }
  int nr_buffers = (seq_header.total_msg_size / handle->mtuBufferSize) + 1;

  struct iovec msg_iovec[MAX_MSG_VECTOR_LEN];
  struct msghdr msg;
  msg.msg_name = dest_addr;
  msg.msg_namelen = addrlen;
  msg.msg_flags = 0;
  msg.msg_iov = msg_iovec;
  msg.msg_iovlen = 2; // header and payload;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;

  msg.msg_iov[0].iov_base = &seq_header;
  msg.msg_iov[0].iov_len = sizeof(seq_header);

  for(n = 0; n < nr_buffers; n++) {
    seq_header.part_msg_size = (((seq_header.total_msg_size - n * handle->mtuBufferSize) >  handle->mtuBufferSize) ?  handle->mtuBufferSize  : (seq_header.total_msg_size - n * handle->mtuBufferSize));
    seq_header.offset        = n * handle->mtuBufferSize;
    int remainingOffset      = seq_header.offset;
    int recvPart = 0;
    // find the start of the part
    while(remainingOffset > large_msg_iovec[recvPart].iov_len) {
      remainingOffset -= large_msg_iovec[recvPart].iov_len;
      recvPart++;
    }
    int remainingData = seq_header.part_msg_size;
    int sendPart = 1;
    msg.msg_iovlen = 1;

    // fill in the output iovec from the input iovec in such a way that all UDP frames are filled maximal.
    while(remainingData > 0) {
      int partLen = ( (large_msg_iovec[recvPart].iov_len - remainingOffset) <= remainingData ? (large_msg_iovec[recvPart].iov_len - remainingOffset) : remainingData);
      msg.msg_iov[sendPart].iov_base = large_msg_iovec[recvPart].iov_base + remainingOffset;
      msg.msg_iov[sendPart].iov_len  = partLen;
      remainingData -= partLen;
      remainingOffset = 0;
      sendPart++;
      recvPart++;
      msg.msg_iovlen++;
    }
    int tmp, tmptot;
    for(tmp = 0, tmptot=0; tmp < msg.msg_iovlen; tmp++) {
      tmptot += msg.msg_iov[tmp].iov_len;
    }

    int w = sendmsg(fd, &msg, 0);
    if(w == -1) {
      fprintf(stderr, "[TCP Socket] Cannot send msg\n");
      result =  -1;
      break;
    }
    written += w;
  }
  handle->writeSeqNr++;
  return (result == 0 ? written : result);
}



int pubsub_tcpBufferHandler_client(pubsub_tcpBufferHandler_pt handle, const char *hostname, int port) {
  int result = 0;
  if (handle->fd<0)   result = pubsub_tcpBufferHandler_get_socket(handle);
  if (handle->efd<0)  result = pubsub_tcpBufferHandler_connect(handle, hostname, port);
  struct epoll_event events[MAX_EPOLL_EVENTS];
  int nof_events = epoll_wait(efd, events, MAX_EPOLL_EVENTS, RECV_THREAD_TIMEOUT);
  if (nof_events < 0) {
    close(handle->efd);
    close(handle->fd);
    handle->efd = -1;
    handle->fd = -1;
    fprintf(stderr, "[TCP Socket] Cannot connect to %s\n", hostname);
    return nof_events;
  }
  int i = 0;
  while (i < nof_events) {
    if (events[i].data.fd == handle->fd) {
      if (events[i].events & EPOLLOUT) {
        pthread_mutex_lock(&handle->dbLock);
        if (!handle->connected) {
          // connection established
          // check pending error if ever
          int err = 0;
          socklen_t len = sizeof (int);
          int connect_result = getsockopt (handle->fd, SOL_SOCKET, SO_ERROR, &err, &len);
          if (connect_result != -1) {
            if (err == 0) fprintf(stderr, "[TCP Socket] Connection established to %s\n", hostname);
            struct epoll_event event;
            bzero(&event, sizeof(epoll_event)); // zero the struct
            event.events = EPOLLIN;
            event.data.fd = handle->fd; // user data
            if (handle->bypassIpFragmentation == false) {
              int rc = getsockopt(handle->fd, IPPROTO_IP, IP_MTU, &handle->mtuSize, &len);
              handle->mtuBufferSize = handle->mtuSize - IP_HEADER_SIZE - TCP_HEADER_SIZE - sizeof(pubsub_tcp_seq_msg_header_t);
              if (rc != 0) fprintf(stderr, "[TCP Socket] Error getsockopt(IP_MTU): %s", strerror(errno));
              if (rc == 0) {
                rc = setsockopt(handle->fd, SOL_SOCKET, SO_SNDBUF, &handle->mtuBufferSize,
                                sizeof(handle->mtuBufferSize));
                if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_SNDBUF): %s", strerror(errno));
                rc = setsockopt(handle->fd, SOL_SOCKET, SO_RCVBUF, &handle->mtuBufferSize,
                                sizeof(handle->mtuBufferSize));
                if (rc != 0) fprintf(stderr, "[TCP Socket] Error setsockopt(SO_RCVBUF): %s", strerror(errno));
              }
            }
            rc = epoll_ctl(handle->efd, EPOLL_CTL_MOD, handle->fd, &event);
            if (rc == -1) {
              if (rc != 0) fprintf(stderr, "[TCP Socket] Error epoll_ctl: %s\n", strerror(errno));
              close(handle->efd);
              close(handle->fd);
              handle->efd = -1;
              handle->fd = -1;
              pthread_mutex_unlock(&handle->dbLock);
              return rc;
            }
          }  else {
            // connection failed
            fprintf(stderr, "[TCP Socket] connection failed: %s\n", strerror(errno));
            struct epoll_event event;
            bzero(&event, sizeof(epoll_event)); // zero the struct
            event.data.fd = handle->fd; // user data
            epoll_ctl (handle->efd, EPOLL_CTL_DEL, handle->fd, &event);
          }
        }
        pthread_mutex_unlock(&handle->dbLock);
      }
    } else {
      unsigned int index = 0;
      unsigned int size  = 0;
      int rc = pubsub_tcpBufferHandler_dataAvailable(handle, events[i].data.fd, &index, &size);
      if() {
        // Handle data
        pubsub_tcp_msg_header_t *msgHeader = NULL;
        void* buffer = NULL;
        if(pubsub_tcpBufferHandler_read(handle, index, &msgHeader, &buffer, size) != 0) {
          fprintf(stderr,("[TCP Socket]: ERROR read with index %d\n", index);
          continue;
        }
        pthread_mutex_lock(&handle->dbLock);
        if (handle->processMsg) {
          struct timespec receiveTime;
          clock_gettime(CLOCK_REALTIME, &receiveTime);
          handle->processMsg(handle->payload, msgHeader, buffer, size, &receiveTime);
        }
        pthread_mutex_unlock(&handle->dbLock);
      }
    }
    i++;
  }
  return efd;
}

int pubsub_tcpBufferHandler_server(pubsub_tcpBufferHandler_pt handle, const char *hostname, int port) {
  int result = 0;
  if (handle->fd<0)   result = pubsub_tcpBufferHandler_get_socket(handle);
  if (handle->efd<0)  result = pubsub_tcpBufferHandler_bind(handle, hostname, port);
  struct epoll_event events[MAX_EPOLL_EVENTS];
  int nof_events = epoll_wait(efd, events, MAX_EPOLL_EVENTS, RECV_THREAD_TIMEOUT);
  if (nof_events < 0) {
    close(handle->efd);
    close(handle->fd);
    handle->efd = -1;
    handle->fd = -1;
    fprintf(stderr, "[TCP Socket] Cannot bind to %s\n", hostname);
    return nof_events;
  }
  int i = 0;
  while (i < nof_events) {
    if (events[i].data.fd == handle->fd) {
      pthread_mutex_lock(&handle->dbLock);
      // new connection available
      sockaddr_in remote;
      socklen_t len = sizeof (sockaddr_in);
      int rc = accept (handle->fd, &remote, &len);
      if (rc == -1) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
          // already closed
        }
        else printf(stderr, "[TCP Socket] accept failed: %s\n", strerror(errno));
      }
      else {
        // handle new connection:
        // add it to reactor, etc
        fprintf(stdout, "[TCP Socket] New connection\n");
      }
      pthread_mutex_unlock(&handle->dbLock);
    } else {
      unsigned int index = 0;
      unsigned int size  = 0;
      if(pubsub_tcpBufferHandler_dataAvailable(handle, events[i].data.fd, &index, &size) == true) {
        // Handle data
        pubsub_tcp_msg_header_t *msgHeader = NULL;
        void* buffer = NULL;
        if(pubsub_tcpBufferHandler_read(handle, index, &msgHeader, &buffer, size) != 0) {
          fprintf(stderr,("[TCP Socket]: ERROR read with index %d\n", index);
          continue;
        }
        pthread_mutex_lock(&handle->dbLock);
        if (handle->processMsg) {
          struct timespec receiveTime;
          clock_gettime(CLOCK_REALTIME, &receiveTime);
          handle->processMsg(handle->payload, msgHeader, buffer, size, &receiveTime);
        }
        pthread_mutex_unlock(&handle->dbLock);
      }
    }
    i++;
  }
  return efd;
}