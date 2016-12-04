/* csci4061 F2016 Assignment 4
* section: 9
* date: 11/30/2016
* names: Tiannan Zhou, Annelies Odermann
* UMN Internet ID, Student ID zhou0745(5232494), oderm008(4740784)
*/

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "util.h"

#define MAX_THREADS 100
#define MAX_QUEUE_SIZE 100
#define MAX_REQUEST_LENGTH 64
#define MAX_PATH_LEN 2048
#define MAX_RTN_LEN 128
#define MAX_BUF_SIZE 524288

char HTML_STR[MAX_RTN_LEN] = "text/html";
char TXT_STR[MAX_RTN_LEN] = "text/plain";
char GIF_STR[MAX_RTN_LEN] = "image/gif";
char JPG_STR[MAX_RTN_LEN] = "image/jpeg";

char ERROR_FILE[MAX_RTN_LEN] = "File not found.";
char ERROR_FBD[MAX_RTN_LEN] = "Permission Denied.";
char ERROR_UNK[MAX_RTN_LEN] = "Unknown error.";
char ERROR_LEN[MAX_RTN_LEN] = "Request too long.";

static pthread_mutex_t buffer_access = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t buffer_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t buffer_empty = PTHREAD_COND_INITIALIZER;

int dispatch_completed;

int port_num;
char path_prefix[1024];
int prefix_len;
int num_dispatcher;
int num_workers;
int queue_length;
int size_cache = 0;

//Structure for queue.
typedef struct request_queue
{
  int m_socket;
  char m_szRequest[MAX_REQUEST_LENGTH];
} request_queue_t;

request_queue_t* stack_req[MAX_QUEUE_SIZE];
int stack_top = 0;
int num_req = 0;

void * dispatch(void * arg)
{
  int fd;
  char filename[1024];
  request_queue_t* req_packet;
  while (1) {
    if ((fd = accept_connection()) < 0) {
      pthread_exit(NULL);
    }
    pthread_mutex_lock(&buffer_access);
    if (get_request(fd, filename) != 0) {
      continue;
    }
    while (stack_top == queue_length) {
      pthread_cond_wait(&buffer_empty, &buffer_access);
    }

    req_packet = (request_queue_t*) malloc(sizeof(request_queue_t));
    req_packet->m_socket = fd;
    strncpy(req_packet->m_szRequest, filename, MAX_REQUEST_LENGTH);
    if (strlen(filename) >= MAX_REQUEST_LENGTH) {
      req_packet->m_szRequest[MAX_REQUEST_LENGTH - 1] = 30;
    }
    stack_req[stack_top] = req_packet;
    stack_top++;

    pthread_cond_signal(&buffer_full);
    pthread_mutex_unlock(&buffer_access);
  }
  return NULL;
}

void * worker(void * arg)
{
  struct stat stat_file_att;
  char* return_type_file;
  int filed;
  int size_file_in_byte, nread;
  int len_full_path;
  char full_path[MAX_PATH_LEN];
  char buf[MAX_BUF_SIZE];
  request_queue_t* req_packet;
  while (1) {
    pthread_mutex_lock(&buffer_access);
    while (stack_top == 0) {
      if (dispatch_completed == 1)
        pthread_exit(NULL);
      pthread_cond_wait(&buffer_full, &buffer_access);
    }

    stack_top--;
    num_req++;
    req_packet = stack_req[stack_top];
    if (req_packet->m_szRequest[MAX_REQUEST_LENGTH - 1] != 30) {
      strncpy(full_path, path_prefix, prefix_len);
      strncpy(full_path + prefix_len,
              req_packet->m_szRequest,
              MAX_REQUEST_LENGTH);
      if ((filed = open(full_path, O_RDONLY)) != -1) {
        len_full_path = strlen(full_path);

        if (len_full_path > 5 &&
            strcmp(full_path + len_full_path - 5, ".html") == 0) {
          return_type_file = HTML_STR;
        } else if (len_full_path > 4 &&
                   strcmp(full_path + len_full_path - 4, ".gif") == 0) {
          return_type_file = GIF_STR;
        } else if (len_full_path > 4 &&
                   strcmp(full_path + len_full_path - 4, ".jpg") == 0) {
          return_type_file = JPG_STR;
        } else {
          return_type_file = TXT_STR;
        }
        // start to read files
        stat(full_path, &stat_file_att);
        size_file_in_byte = stat_file_att.st_size;
        printf("size got: %d\n", size_file_in_byte);
        nread = read(filed, buf, size_file_in_byte);
        printf("size read: %d\n", nread);
        size_file_in_byte = nread;
        return_result(req_packet->m_socket,
                      return_type_file,
                      buf,
                      size_file_in_byte);
        close(filed);
      } else {
        // have difficulty to reach the file
        if (errno == ENOENT) {
          return_error(req_packet->m_socket, ERROR_FILE);
        } else if (errno == EACCES) {
          return_error(req_packet->m_socket, ERROR_FBD);
        } else {
          return_error(req_packet->m_socket, ERROR_UNK);
        }
      }
    } else {
      // the request is too long
      return_error(req_packet->m_socket, ERROR_LEN);
    }
    free(req_packet);
    pthread_cond_signal(&buffer_empty);
    pthread_mutex_unlock(&buffer_access);
  }
  return NULL;
}

int main(int argc, char **argv)
{
  //Error check first.
  pthread_t t_dispathers[MAX_THREADS];
  pthread_t t_workers[MAX_THREADS];

  if(argc != 6 && argc != 7)
  {
    printf("usage: %s port path num_dispatcher num_workers queue_length [cache_size]\n", argv[0]);
    return -1;
  }

  port_num = atoi(argv[1]);
  strncpy(path_prefix, argv[2], 1024);
  prefix_len = strlen(path_prefix);
  num_dispatcher = atoi(argv[3]);
  if (num_dispatcher <= 0 || num_dispatcher > MAX_THREADS) {
    printf("Please enter a valid num_dispatcher (1-%d)\n", MAX_THREADS);
    return -1;
  }
  num_workers = atoi(argv[4]);
  if (num_workers <= 0 || num_workers > MAX_THREADS) {
    printf("Please enter a valid num_workers (1-%d)\n", MAX_THREADS);
    return -1;
  }
  queue_length = atoi(argv[5]);
  if (queue_length > MAX_QUEUE_SIZE) {
    printf("Please enter a valid queue_length (1-%d)\n", MAX_QUEUE_SIZE);
    return -1;
  }
  if (argc == 7) {
    size_cache = atoi(argv[6]);
  } else {
    size_cache = 0;
  }

  dispatch_completed = 0;

  init(port_num);

  int i;
  for (i = 0; i < num_dispatcher; i++) {
    if (pthread_create(&t_dispathers[i], NULL, dispatch, NULL) != 0) {
      printf("Error creating dispatch thread\n");
      exit(1);
    }
  }
  for (i = 0; i < num_workers; i++) {
    if (pthread_create(&t_workers[i], NULL, worker, (void *) &i) != 0) {
      printf("Error creating worker thread\n");
      exit(1);
    }
  }

  for (i = 0; i < num_dispatcher; i++) {
    if (pthread_join(t_dispathers[i], NULL) != 0)
      printf("Error joining dispatch thread\n");
  }
  dispatch_completed = 1;
  for (i = 0; i < num_workers; i++) {
    if (pthread_join(t_workers[i], NULL) != 0)
      printf("Error joining worker thread\n");
  }
  return 0;
}
