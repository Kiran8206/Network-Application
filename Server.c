//Name : Kiran Kumar Raju Ganga Raju :: Student ID : 11333047 :: Course : CSCE 5580 :: Date : 03/29/2019
//This C program implements client-server multi-threaded network programming with parallel support to 20 clients


// Includes
//
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/time.h>

//
// Defines
//
#define RS_OK       			0
#define RS_NOT_OK   			1
#define MAX_CLIENT_CONNECTION 	20
#define BUFF_SIZE 				100000
#define BACK_LOG    			10
#define STR_NULL				'\0'

#define EXIT_STATE end
#define ENTRY_STATE entry

// 
// Typedef
//
typedef int bool;

//
// Enum
//
enum { false, true };

enum state_codes { entry, connected, end };

enum ret_codes { join, pong, list, leave, repeat, fail, already };

//
// Structures
//
struct SocketCore {
	int socketID;
	char address[64];
	int port;
	int rtt;
};

struct SocketClient
{
	pthread_t tid;
	int    socket;
	struct sockaddr_in sockAddr;
	char message[BUFF_SIZE];
	char username[256];
	struct timeval startT;
	double  rtt;
	bool    pingFlag;
};

struct SocketServer
{
	struct sockaddr_in sockAddr;
	uint64_t socketPool[MAX_CLIENT_CONNECTION];
	bool isStop;
};

struct transition {
	enum state_codes src_state;
	enum ret_codes   ret_code;
	enum state_codes dst_state;
	char message[1024];
};

struct transition state_transitions[] = {
	{entry,		join,		connected,		"PING\n"},
	{entry,		pong,		entry,			"Unregistered User. Use \"JOIN <username>\" to Register.\n"},
	{entry,		list,		entry,			"Unregistered User. Use \"JOIN <username>\" to Register.\n"},
	{entry,		leave,		end,			""},
	{entry,		repeat,		entry,			"Unregistered User. Use \"JOIN <username>\" to Register.\n"},
	{entry,		fail,		end,			"Client disconnected\n"},
	{entry,		already,	entry,			""},
	{connected, join,		connected,		""},
	{connected, already,	connected,		""},
	{connected, pong,		connected,		""},
	{connected, list,		connected,		""},
	{connected, leave,		end,			""},
	{connected, repeat,		connected,		"PING\n"},
	{connected, fail,		end,			"Client disconnected\n"} };

//
// Global Functions
//
int EntryState(void* _socketClient);
int ConnectedState(void* _socketClient);
int ExitState(void* _socketClient);
int StartSocketServer(int _port);
void *ConnectionHandler(void *socket_desc);
void     INThandler(int);
void LowerStr(char* _str);

int(*state[])(void*) = { EntryState, ConnectedState, ExitState };

//
// Global Variables
//
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct SocketServer socketServer;
int i;

int main(int argc, char *argv[])
{
	if (argc < 2 || argc > 2)
	{
		puts("usage: ./server <svr port>\n");
		return RS_NOT_OK;
	}
	else
	{
		char* socketModeStr = argv[0];
		char* portStr = argv[1];

		signal(SIGINT, INThandler);
		LowerStr(socketModeStr);
		StartSocketServer(atoi(portStr));
	}
}

void  INThandler(int sig)
{
     signal(sig, SIG_IGN);
     exit(0);
}

void LowerStr(char* _str)
{
	if (_str == NULL)
		return;

	for (i = 0; _str[i]; i++) {
		_str[i] = (char)tolower(_str[i]);
	}
}

char* LookupMessageTransitions(enum state_codes _cur_state, enum ret_codes _rc)
{
	size_t n = sizeof(state_transitions) / sizeof(struct transition);
	for (i = 0; i < n; i++)
	{
		if (state_transitions[i].src_state == _cur_state && state_transitions[i].ret_code == _rc)
		{
			return state_transitions[i].message;
		}
	}
	return "";
}

int BuildMessageInfoForClient(char* _str)
{
	int res = -1;
	strcpy(_str, "USERNAME\t FD\t RTT\n");
	strcat(_str, "----------------------------------\n");
	pthread_mutex_lock(&mutex);
	for (i = 0; i < MAX_CLIENT_CONNECTION; i++)
	{
		if (socketServer.socketPool[i] != 0)
		{
			struct SocketClient* temp = (struct SocketClient*)socketServer.socketPool[i];
			if (strcmp(temp->username, ""))
			{
				char str[256];
				str[0] = STR_NULL;
				sprintf(str, "%s\t %d\t ", temp->username, temp->socket);
				if (temp->rtt == 0)
					sprintf(str, "%sN/a\n", str);
				else 
					sprintf(str, "%s%lf ms\n", str, temp->rtt);
				strcat(_str, str);
			}
		}
	}
	pthread_mutex_unlock(&mutex);
	return res;
}

bool CheckAvailabelUsername(char* _strUsername)
{
	int res = false;

	pthread_mutex_lock(&mutex);
	for (i = 0; i < MAX_CLIENT_CONNECTION; i++)
	{
		if (socketServer.socketPool[i] != 0)
		{
			struct SocketClient* temp = (struct SocketClient*)socketServer.socketPool[i];
			if (!strcmp(temp->username, _strUsername)) {
				res = true;
				break;
			}
		}
	}
	pthread_mutex_unlock(&mutex);
	return res;
}

enum ret_codes SocketReceived(struct SocketClient* _socketClient)
{
	char trace[256];
	int read_size = 0;
	_socketClient->message[0] = '\0';
	if ((read_size = (int)recv(_socketClient->socket, _socketClient->message, BUFF_SIZE, 0)) > 0)
	{
		//end of string marker
		_socketClient->message[read_size] = '\0';
		if (strcmp(_socketClient->message, "\r\n"))
		{
			char * pch;

			pch = strtok(_socketClient->message, " \r\n");
			LowerStr(pch);

			if (pch != NULL) {
				if (!strcmp(pch, "leave"))
					return leave;
				else if (!strcmp(pch, "list"))
					return list;
				else if (!strcmp(pch, "pong")) {
					if (_socketClient->startT.tv_usec != 0)
					{
						struct timeval endT;
						gettimeofday(&endT, 0);
						double difference = (endT.tv_usec - _socketClient->startT.tv_usec) / 1000.0f + (endT.tv_sec - _socketClient->startT.tv_sec) * 1000;
						_socketClient->startT.tv_usec = 0;
						_socketClient->rtt = difference;
						_socketClient->pingFlag = false;
					}
					else
						_socketClient->pingFlag = true;
					return pong;
				}
				else if (!strcmp(pch, "join")) {
					if (_socketClient->username[0]) {
						sprintf(trace, "User Already Registered. Username (%s), FD (%d)\n", _socketClient->username, _socketClient->socket);
						write(_socketClient->socket, trace, strlen(trace)); //send response to client
						return already;
					}
					else {
						pch = strtok(NULL, " \r\n");
						if (pch != NULL) {
							if (!CheckAvailabelUsername(pch)) //Username doen't exist in database
							{
								strcpy(_socketClient->username, pch);
								return join;
							}
							else
							{
								sprintf(trace, "User Already Registered. Username (%s), FD (%d)\n", pch, _socketClient->socket);
								write(_socketClient->socket, trace, strlen(trace)); //send response to client
								return already;
							}
						}
						else
							return repeat;
					}
				}
				else {
					sprintf(trace, "Client (%d): Unrecognizable Message. Resending PING.", _socketClient->socket);
					puts(trace);
					return repeat;
				}
			}
			else
				return repeat;
		}
		else
			return repeat;
	}

	if (read_size == 0)
	{
		puts("Client disconnected");
		fflush(stdout);
		return end;
	}
	else if (read_size == -1)
	{
		perror("recv failed");
		return fail;
	}
	return fail;
}

int EntryState(void* _socketClient)
{
	struct SocketClient* socketClient = _socketClient;
	enum ret_codes rc = SocketReceived(socketClient);
	char * message = LookupMessageTransitions(entry, rc);
	char trace[255];
	char* pch = strtok(socketClient->message, " \r\n");

	if (rc != join) {
		if (strcmp(pch, "leave") == 0) {
			sprintf(trace, "Client (%d): %s", socketClient->socket, pch);
			puts(trace);
			sprintf(trace, "Unable Locate Client (%d) in Database. Disconnecting User. ", socketClient->socket);
			printf("%s", trace);
		}
		else if (strcmp(pch, "join") == 0) {
			sprintf(trace, "Client (%d): User Already Registered. Discarding JOIN", socketClient->socket);
			puts(trace);
		}
		else {
			sprintf(trace, "Unable Locate Client (%d) in Database. Discarding %s", socketClient->socket, pch);
			puts(trace);
		}
	}
	else {
		sprintf(trace, "Client (%d): %s", socketClient->socket, pch);
		puts(trace);
	}

	if (strcmp(message, ""))
	{

		if (!strcmp(message, "PING\n"))
		{
			socketClient->rtt = 0;
			gettimeofday(&(socketClient->startT), 0);
		}
		write(socketClient->socket, message, strlen(message)); //send response to client
	}
	else {
	}
	return rc;
}

int ConnectedState(void* _socketClient)
{
	char trace[256];
	struct SocketClient* socketClient = _socketClient;
	enum ret_codes rc = SocketReceived(socketClient);
	char * message = LookupMessageTransitions(connected, rc);
	char* pch = strtok(socketClient->message, " \r\n");

	if (strcmp(pch, "join") == 0) {
		sprintf(trace, "Client (%d): User Already Registered. Discarding JOIN", socketClient->socket);
		puts(trace);
	}
	else {
		sprintf(trace, "Client (%d): %s", socketClient->socket, pch);
		puts(trace);
	}

	if (strcmp(message, ""))
	{
		if (!strcmp(message, "PING\n"))
		{
			socketClient->rtt = 0;
			gettimeofday(&(socketClient->startT), 0);

		}
		write(socketClient->socket, message, strlen(message)); //send response to client
	}
	else
	{
		if (rc == list)
		{
			char resMessage[1024];
			resMessage[0] = STR_NULL;
			BuildMessageInfoForClient(&resMessage[0]);
			write(socketClient->socket, resMessage, strlen(resMessage)); //send response to client
		}
		else if (rc == pong && socketClient->pingFlag)
		{
			socketClient->rtt = 0;
			gettimeofday(&(socketClient->startT), 0);
			sprintf(trace, "Sent PING request for socket %d", socketClient->socket);
			puts(trace);
			write(socketClient->socket, "PING\n", strlen("PING\n")); //send response to client
			socketClient->pingFlag = false;
		}
	}
	return rc;
}

int ExitState(void* _socketClient)
{
	struct SocketClient* socketClient = _socketClient;
	pthread_mutex_lock(&mutex);
	for (i = 0; i < MAX_CLIENT_CONNECTION; i++)
	{
		if (socketServer.socketPool[i] != 0)
		{
			struct SocketClient* temp = (struct SocketClient*)socketServer.socketPool[i];
			if (socketClient->socket == temp->socket)
			{
				char trace[256];
				socketServer.socketPool[i] = 0;
				close(temp->socket);
				sprintf(trace, "Client (%d): Disconnecting User.", socketClient->socket);
				puts(trace);
				free(temp);
			}
		}
	}
	pthread_mutex_unlock(&mutex);
	return fail;
}

int AssignAvailableSlot(struct SocketClient* _socketClient)
{
	int res = -1;
	pthread_mutex_lock(&mutex);
	for (i = 0; i < MAX_CLIENT_CONNECTION; i++)
	{
		if (socketServer.socketPool[i] == 0)
		{
			socketServer.socketPool[i] = (uint64_t)_socketClient;
			res = i;
			break;
		}

	}
	pthread_mutex_unlock(&mutex);
	return res;
}

void ReleaseSocketServer()
{
	for (i = 0; i < MAX_CLIENT_CONNECTION; i++)
	{
		if (socketServer.socketPool[i] != 0)
		{
			struct SocketClient* socketClient = (struct SocketClient*)socketServer.socketPool[i];
			pthread_join(socketClient->tid, NULL);
			socketServer.socketPool[i] = 0;
			close(socketClient->socket);
			char trace[256];
			sprintf(trace, "Client %d disconnected", socketClient->socket);
			puts(trace);
			free(socketClient);
		}
	}
}

int StartSocketServer(int _port)
{
	int socket_desc, c;

	//Create socket
	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket");
		return RS_NOT_OK;
	}

	//Prepare the sockaddr_in structure
	socketServer.sockAddr.sin_family = AF_INET;
	socketServer.sockAddr.sin_addr.s_addr = INADDR_ANY;
	socketServer.sockAddr.sin_port = htons((uint16_t)_port);

	//Bind
	if (bind(socket_desc, (struct sockaddr *)&socketServer.sockAddr, sizeof(socketServer.sockAddr)) < 0)
	{
		//print the error message
		perror("bind failed. Error");
		return RS_NOT_OK;
	}

	//Listen
	if (listen(socket_desc, BACK_LOG) != 0)
	{
		//print the error message
		perror("listen failed. Error");
		close(socket_desc);
		return RS_NOT_OK;
	}

	//Accept and incoming connection
	puts("Waiting for incoming connections...");
	c = sizeof(struct sockaddr_in);

	struct SocketClient* socketClient = malloc(sizeof(struct SocketClient));
	memset(socketClient, 0, sizeof(struct SocketClient));

	while (!socketServer.isStop)
	{
		socketClient->socket = accept(socket_desc, (struct sockaddr *)&socketClient->sockAddr, (socklen_t*)&c);
		if (socketClient->socket != -1)
		{
			char trace[256];
			sprintf(trace, "Client (%d): Connection Accepted", socketClient->socket);
			puts(trace);
			if (AssignAvailableSlot(socketClient) != -1)
			{
				sprintf(trace, "Client (%d): Handler Assigned", socketClient->socket);
				puts(trace);
				if (pthread_create(&socketClient->tid, NULL, ConnectionHandler, (void*)socketClient) < 0)
				{
					puts("could not create thread");
				}
				else
				{
					socketClient = malloc(sizeof(struct SocketClient));
					memset(socketClient, 0, sizeof(struct SocketClient));
				}
			}
			else
			{
				char* ms = "Error: Too Many Clients Connected ...";
				char* sendms = "Too Many Users. Disconnecting User.";
				sprintf(trace, "Client (%d): Databae Full. Disconnecting User.", socketClient->socket);
				puts(trace); 
				puts(ms);

				write(socketClient->socket, sendms, strlen(sendms)); //send response to client
				close(socketClient->socket);
			}

		}
		sleep(10);
	}

	ReleaseSocketServer();
	close(socket_desc);
	return RS_OK;
}

enum state_codes LookupStateTransitions(enum state_codes _cur_state, enum ret_codes _rc)
{
	size_t n = sizeof(state_transitions) / sizeof(struct transition);
	for (i = 0; i < n; i++)
	{
		if (state_transitions[i].src_state == _cur_state && state_transitions[i].ret_code == _rc)
		{
			return state_transitions[i].dst_state;
		}
	}
	return end;
}

void *ConnectionHandler(void *_socketClient)
{
	enum state_codes cur_state = ENTRY_STATE;
	enum ret_codes rc;
	int(*state_fun)(void*);
	for (;;)
	{
		state_fun = state[cur_state];
		rc = state_fun(_socketClient);
		if (EXIT_STATE == cur_state)
			break;
		cur_state = LookupStateTransitions(cur_state, rc);
	}
	return 0;
}
