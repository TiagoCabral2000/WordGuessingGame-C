#pragma once

#ifndef STRUCT
#define STRUCT

#include <tchar.h>
#include <windows.h>

#define SHM_NAME _T("SHM")
#define MUTEX_WRITE_NAME _T("MUTEX_WRITE")
#define MUTEX_READ_NAME _T("MUTEX_READ")
#define MUTEX_SERVER_NAME _T("MUTEX_SERVER")
#define MAXCLIENTES 20
#define MAX_WORDS 10
#define MAXLETRAS 12
#define WORD_LEN 16
#define GmMsg_size sizeof(GameMessage)

// Message types
#define MSG_NEW_CLIENT 1
#define MSG_WORD_GUESS 2
#define MSG_LIST_REQUEST 3
#define MSG_PONT_REQUEST 4
#define MSG_INFO 5
#define MSG_WORD_FEEDBACK 6
#define MSG_NAME_FEEDBACK 7
#define MSG_EXIT 8
#define MSG_SHUTDOWN 9 

typedef struct {
    int type;
    TCHAR clientName[50];
    TCHAR wordGuess[20];
    TCHAR msg[100];
    BOOL correct;
    BOOL name_feedback;
    int points;
} GameMessage;

typedef struct {
    unsigned int idArbitro;
    unsigned int idJogador;
    unsigned int writePos;
    unsigned int readPos;
    TCHAR buffer[MAXLETRAS];
    unsigned int numClientesAtivos;
    DWORD numLetras;
    DWORD ritmo;
} SharedMem;

typedef struct {
    SharedMem* shm;
    HANDLE hMapFile, hWriteSem, hReadSem, hMutex;
    BOOL exit;
    int id;
    char letra;
    int maxPontos;
} ControlData;

typedef struct {
    HANDLE hPipe;
    TCHAR name[50];
    int pontuacao;
} ClientInfo;


#endif