#ifndef CHATROOM_UTILS_H_INCLUDED
#define CHATROOM_UTILS_H_INCLUDED

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>
#include <time.h>

#define MSGSZ     150
#define MAX_CLIENTS 4

/* enum of different messages possible */
typedef enum {
  CONNECT = 1,
  DISCONNECT,
  GET_USERS,
  SET_USERNAME,
  PUBLIC_MESSAGE,
  PRIVATE_MESSAGE,
  TOO_FULL,
  USERNAME_ERROR,
  SUCCESS,
  ERROR,
  ALIVE,
  NONE
} MessageType;


/* message structure */
typedef struct message {
    MessageType   mtype;
    bool is_taken;
    char username[20];
    char    message[MSGSZ];
} MessageBuf;

int CreateSegments(void);
int AttachSegments(void);
void ClearServerBuf(void);
void ClearClientsBuf(void);
int OpenSemaphores(void);
void CleanSemaphores(void);
int InitializeServer(void);
int RefreshUserList(void);
int HandlePublicMessage(void);
int HandlePrivateMessage(void);
int DisconnectUser(void);
void *HandleClientMsg(void *args);
void *SetServerStatus(void *args);
void GetBackSem(void);
void *CheckClientStatus(void *args);
void CleanUp(void);

#endif
