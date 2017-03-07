#include "chatroom_utils.h"

/* global variables for work with threads */
int shmid_server;
int shmid_clients;

bool turn_off = false;
MessageBuf *shmid_server_buf;
MessageBuf *shmid_clients_buf;

key_t key_server = 5;
key_t key_clients = 10;

/* message communication semaphores */
sem_t *msg_mutex_server;
sem_t *msg_mutex_clients;
const char MSG_SEM_SERVER[] = "sync_server";
const char MSG_SEM_CLIENT[] = "sync_clients";

/* status check semaphores */
sem_t *stat_clients[MAX_CLIENTS];
sem_t *stat_server;
const char STAT_CLIENTS[] = "client_status";
const char STAT_IN[] = "server_status";

/* create shm segments for message communication and status check */
int CreateSegments(void) {
  int shmflg = IPC_CREAT | 0666;

  if ((shmid_server = shmget(key_server, sizeof(MessageBuf), shmflg)) < 0) {
    perror("shmget");
    exit(1);
  }

  if ((shmid_clients = shmget(key_clients, sizeof(MessageBuf) * MAX_CLIENTS, shmflg))
      < 0) {
    perror("shmget");
    return -1;
  }

  return 0;
}

/* attach shared memory segments */
int AttachSegments(void) {
  if ((shmid_server_buf = shmat(shmid_server, NULL, 0)) == (MessageBuf *) -1) {
    perror("shmat");
    return -1;
  }

  if ((shmid_clients_buf = shmat(shmid_clients, NULL, 0)) == (MessageBuf *) -1) {
    perror("shmat");
    return -1;
  }

  return 0;
}

/* clear server memory segment */
void ClearServerBuf(void) {
  shmid_server_buf->mtype = NONE;
  shmid_server_buf->is_taken = false;
  memset(shmid_server_buf->username, 0, 20);
  memset(shmid_server_buf->message, 0, MSGSZ);
}

/* clear clients memory segment */
void ClearClientsBuf(void) {
  int i;

  for (i = 0; i < MAX_CLIENTS; i++) {
    shmid_clients_buf[i].mtype = NONE;
    shmid_clients_buf[i].is_taken = false;
    memset(shmid_clients_buf[i].username, 0, 20);
    memset(shmid_clients_buf[i].message, 0, MSGSZ);
  }
}

/* open semaphores for message communication and status check */
int OpenSemaphores(void) {
  int i;
  /* create & initialize semaphore */
  /* open message semaphore */
  msg_mutex_server = sem_open(MSG_SEM_SERVER, O_CREAT, 0644, 1);
  if (msg_mutex_server == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(MSG_SEM_SERVER);
    return -1;
  }

  msg_mutex_clients = sem_open(MSG_SEM_CLIENT, O_CREAT, 0644, 1);
  if (msg_mutex_clients == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(MSG_SEM_CLIENT);
    return -1;
  }

  /* open status check on client semaphore */
  stat_server = sem_open(STAT_IN, O_CREAT, 0644, 1);
  if (stat_server == SEM_FAILED) {
    perror("Unable to create semaphore\n");
    sem_unlink(STAT_IN);
    return -1;
  }

  /* open status check on server semaphore */
  for (i = 0; i < MAX_CLIENTS; i++) {
    stat_clients[i] = sem_open(STAT_CLIENTS, O_CREAT, 0644, 1);
    if (stat_clients[i] == SEM_FAILED) {
      perror("Unable to create semaphore\n");
      sem_unlink(STAT_CLIENTS);
      return -1;
    }
  }

  return 0;
}

void CleanSemaphores(void) {
  int sval;
  int i;

  sem_getvalue(msg_mutex_server, &sval);
  if (sval < 1)
    sem_post(msg_mutex_server);

  sem_getvalue(msg_mutex_clients, &sval);
  if (sval < 1)
    sem_post(msg_mutex_clients);

  sem_getvalue(stat_server, &sval);
  if (sval < 1)
    sem_post(stat_server);

  for (i = 0; i < MAX_CLIENTS; i++) {
    sem_getvalue(stat_clients[i], &sval);
    if (sval < 1)
      sem_post(stat_clients[i]);
  }
}

/* create message queues */
int InitializeServer(void) {
  if (CreateSegments() < 0)
    return -1;
  else if (AttachSegments() < 0)
    return -1;
  else if (OpenSemaphores() < 0)
    return -1;
  else {
    ClearServerBuf();
    ClearClientsBuf();
    CleanSemaphores();
  }
  return 0;
}

/* add client to clients array */
int RefreshUserList(void) {
  int i;
  for (i = 0; i < MAX_CLIENTS; i++) {
    if (shmid_clients_buf[i].is_taken) {
      shmid_clients_buf[i].mtype = GET_USERS;
    }
  }
  return 0;
}

/* if the message is public - send it to all clients */
int HandlePublicMessage(void) {
  int i;
  char message[128];

  strcpy(message, shmid_server_buf->username);
  strcat(message, ": ");
  strcat(message, shmid_server_buf->message);

  for (i = 0; i < MAX_CLIENTS; i++) {
    if (shmid_clients_buf[i].is_taken) {
      shmid_clients_buf[i].mtype = PUBLIC_MESSAGE;
      strcpy(shmid_clients_buf[i].message, message);
    }
  }

  return 0;
}

/* if the message is private - send it to certain user */
int HandlePrivateMessage(void) {
  int i = 1;
  char message[128];
  char usr_src[20];
  char usr_dest[20];

  strcpy(usr_src, shmid_server_buf->username);
  strcpy(message, shmid_server_buf->username);
  strcat(message, ": ");

  /* cut out the username, located between /username/ */
  while (shmid_server_buf->message[i] != '/') {
    usr_dest[i - 1] = shmid_server_buf->message[i];
    i++;
  }
  i++;

  /* copy message without username destination string */
  strcat(message, shmid_server_buf->message + i);

  for (i = 0; i < MAX_CLIENTS; i++) {
    if ((strcmp(usr_dest, shmid_clients_buf[i].username) == 0
        || strcmp(usr_src, shmid_clients_buf[i].username) == 0)
        && shmid_clients_buf[i].is_taken) {
      shmid_clients_buf[i].mtype = PRIVATE_MESSAGE;
      strcpy(shmid_clients_buf[i].message, message);
    }
  }
  return 0;
}

/* disconnect client */
int DisconnectUser(void) {
  int i;

  for (i = 0; i < MAX_CLIENTS; i++) {
    if (strcmp(shmid_server_buf->username, shmid_clients_buf[i].username) == 0
        && shmid_clients_buf[i].is_taken) {
      memset(shmid_clients_buf[i].username, 0, 20);
      shmid_clients_buf[i].is_taken = false;
      printf("User %s has disconnected\n", shmid_clients_buf[i].username);
    } else {
      shmid_clients_buf[i].mtype = GET_USERS;
    }
  }
  return 0;
}

/* thread responsible for incoming message handling */
void *HandleClientMsg(void *args) {
  while (!turn_off) {
    sem_wait(msg_mutex_server);
    sem_wait(msg_mutex_clients);
    switch (shmid_server_buf->mtype) {
      case CONNECT:
        RefreshUserList();
        break;
      case PUBLIC_MESSAGE:
        HandlePublicMessage();
        break;
      case PRIVATE_MESSAGE:
        HandlePrivateMessage();
        break;
      case DISCONNECT:
        DisconnectUser();
        break;
      default:
        break;
    }
    ClearServerBuf();
    sem_post(msg_mutex_clients);
    sem_post(msg_mutex_server);
  }
  pthread_exit(NULL);
}

/* thread responsible for sending messages
 * to clients that server is alive */
void *SetServerStatus(void *args) {
  int sval;
  while (!turn_off) {
    sem_getvalue(stat_server, &sval);
    if (sval < MAX_CLIENTS)
      sem_post(stat_server);
  }
  pthread_exit(NULL);
}
void GetBackSem(void) {
  /* timeout to get back stuked semaphores
   * if client turned off his machine */
  struct timespec ts;

  ts.tv_sec = 2;
  sem_timedwait(msg_mutex_server, &ts);
  sem_post(msg_mutex_server);
  ts.tv_sec = 2;

  sem_timedwait(msg_mutex_clients, &ts);
  sem_post(msg_mutex_clients);
  ts.tv_sec = 2;
}
/* thread responsible for checking
 * whether the client is alive */
void *CheckClientStatus(void *args) {
  int i;
  int status;

  while (!turn_off) {
    sleep(2);
    for (i = 0; i < MAX_CLIENTS; i++) {
      status = sem_trywait(stat_clients[i]);
      /* if user disconnect (not updating his semaphore value) */
      GetBackSem();
      sem_wait(msg_mutex_clients);
      if (status < 0 && shmid_clients_buf[i].is_taken && shmid_clients_buf[i].mtype != CONNECT) {
        printf("User %s has disconnected\n", shmid_clients_buf[i].username);
        shmid_clients_buf[i].is_taken = false;
        memset(shmid_clients_buf[i].username, 0, 20);
        RefreshUserList();
      }
      sem_post(msg_mutex_clients);
    }
  }

  pthread_exit(NULL);
}

/* before exit clear shm and semaphores */
void CleanUp(void) {
  sem_close(msg_mutex_server);
  sem_unlink(MSG_SEM_SERVER);
  shmctl(shmid_clients, IPC_RMID, 0);
  shmctl(shmid_server, IPC_RMID, 0);
}
int main(void) {
  int i;
  char input[2];
  pthread_t server_func[2];
  pthread_attr_t attr;

  if (InitializeServer() < 0) {
    return EXIT_FAILURE;
  }

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  pthread_create(&server_func[0], NULL, HandleClientMsg, NULL);
  pthread_create(&server_func[1], NULL, SetServerStatus, NULL);
  pthread_create(&server_func[2], NULL, CheckClientStatus, NULL);

  pthread_attr_destroy(&attr);

  printf("Type /q to turn off the server\n");

  while (strcmp(input, "/q") != 0)
    scanf("%2s", input);

  turn_off = true;
  for (i = 0; i < 3; i++) {
    pthread_join(server_func[i], NULL);
  }

  CleanUp();

  return EXIT_SUCCESS;
}
