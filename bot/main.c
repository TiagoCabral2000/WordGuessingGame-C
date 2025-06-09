#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include "struct.h"

#define BUFSIZE 2048
#define DEFAULT_REACTION_TIME 3
#define DICTIONARY_SIZE 10

const TCHAR* dictionary[DICTIONARY_SIZE] = {
    _T("VERMELHO"),
    _T("AZUL"),
    _T("AMARELO"),
    _T("VERDE"),
    _T("ROSA"),
    _T("ROXO"),
    _T("LARANJA"),
    _T("PRETO"),
    _T("BRANCO"),
    _T("CINZA")
};

HANDLE hConsoleMutex;
HANDLE hEvSai;
HANDLE hPipe;
int DeveContinuar = 1;
int reactionTime = DEFAULT_REACTION_TIME;

void PrintLastError(TCHAR* part, DWORD id) {
    LPTSTR buffer;
    if (part == NULL) part = TEXT("*");
    if (id == 0) id = GetLastError();
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        id,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&buffer,
        64,
        NULL);
    WaitForSingleObject(hConsoleMutex, INFINITE);
    _tprintf(TEXT("\n%s Erro %d: %s\n"), part, id, buffer);
    ReleaseMutex(hConsoleMutex);
    LocalFree(buffer);
}

DWORD WINAPI PipeReaderThread(LPVOID lpvParam) {
    HANDLE hPipe = (HANDLE)lpvParam;
    HANDLE hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ov = { 0 };
    ov.hEvent = hReadEvent;

    GameMessage serverMsg;
    DWORD bytesRead;

    while (DeveContinuar) {
        ResetEvent(hReadEvent);
        if (!ReadFile(hPipe, &serverMsg, sizeof(GameMessage), &bytesRead, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                PrintLastError(TEXT("Error reading from pipe"), GetLastError());
                break;
            }
        }

        HANDLE handles[2] = { hReadEvent, hEvSai };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) {
            GetOverlappedResult(hPipe, &ov, &bytesRead, FALSE);

            if (bytesRead != sizeof(GameMessage)) {
                WaitForSingleObject(hConsoleMutex, INFINITE);
                _tprintf(TEXT("\nIncomplete message received from server"));
                ReleaseMutex(hConsoleMutex);
                continue;
            }

            switch (serverMsg.type) {
            case MSG_NAME_FEEDBACK:
                if (!serverMsg.name_feedback) {
                    WaitForSingleObject(hConsoleMutex, INFINITE);
                    _tprintf(TEXT("\n%s"), serverMsg.msg);
                    ReleaseMutex(hConsoleMutex);
                    DeveContinuar = 0;
                    SetEvent(hEvSai);
                }
                break;

            case MSG_INFO:
                WaitForSingleObject(hConsoleMutex, INFINITE);
                _tprintf(TEXT("\n[INFO] %s"), serverMsg.msg);
                ReleaseMutex(hConsoleMutex);
                break;

            case MSG_WORD_FEEDBACK:
                WaitForSingleObject(hConsoleMutex, INFINITE);
                _tprintf(TEXT("\n[RESULT] %s"), serverMsg.msg);
                ReleaseMutex(hConsoleMutex);
                break;

            case MSG_SHUTDOWN:
                WaitForSingleObject(hConsoleMutex, INFINITE);
                _tprintf(TEXT("\n%s"), serverMsg.msg);
                ReleaseMutex(hConsoleMutex);
                DeveContinuar = 0;
                SetEvent(hEvSai);
                break;

            default:
                WaitForSingleObject(hConsoleMutex, INFINITE);
                _tprintf(TEXT("\nUnknown message type: %d"), serverMsg.type);
                ReleaseMutex(hConsoleMutex);
                break;
            }
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) {
            break;
        }
    }

    CloseHandle(hReadEvent);
    return 0;
}

DWORD WINAPI BotBehaviorThread(LPVOID lpvParam) {
    GameMessage* initMsg = (GameMessage*)lpvParam;
    TCHAR botName[50];
    _tcscpy_s(botName, 50, initMsg->clientName);

    srand((unsigned int)time(NULL) + GetCurrentThreadId());

    while (DeveContinuar) {
        Sleep(reactionTime * 1000);

        if (!DeveContinuar) break;

        //Escolhe palavra random
        const TCHAR* word = dictionary[rand() % DICTIONARY_SIZE];

        GameMessage guessMsg;
        guessMsg.type = MSG_WORD_GUESS;
        _tcscpy_s(guessMsg.clientName, 50, botName);
        _tcscpy_s(guessMsg.wordGuess, 20, word);

        HANDLE hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        OVERLAPPED ovWrite = { 0 };
        ovWrite.hEvent = hWriteEvent;
        DWORD cbWritten;

        if (!WriteFile(hPipe, &guessMsg, sizeof(GameMessage), &cbWritten, &ovWrite)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                PrintLastError(TEXT("Error sending guess"), GetLastError());
                CloseHandle(hWriteEvent);
                break;
            }
            WaitForSingleObject(hWriteEvent, INFINITE);
        }
        GetOverlappedResult(hPipe, &ovWrite, &cbWritten, FALSE);
        CloseHandle(hWriteEvent);

        if (cbWritten == sizeof(GameMessage)) {
            WaitForSingleObject(hConsoleMutex, INFINITE);
            _tprintf(TEXT("\n[%s] Trying word: %s"), botName, word);
            ReleaseMutex(hConsoleMutex);
        }
        else {
            WaitForSingleObject(hConsoleMutex, INFINITE);
            _tprintf(TEXT("\n[%s] Failed to send guess"), botName);
            ReleaseMutex(hConsoleMutex);
            break;
        }
    }

    return 0;
}

int _tmain(int argc, TCHAR* argv[]) {
#ifdef UNICODE
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif

    if (argc < 2) {
        _tprintf(TEXT("Usage: bot <username> [reaction_time]\n"));
        return 1;
    }

    // Parse reaction time if provided
    if (argc >= 3) {
        reactionTime = _ttoi(argv[2]);
        if (reactionTime < 1) reactionTime = DEFAULT_REACTION_TIME;
    }

    hConsoleMutex = CreateMutex(NULL, FALSE, NULL);
    hEvSai = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Connect to server pipe
    LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\tpSO2");
    hPipe = CreateFile(
        lpszPipename,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);

    if (hPipe == INVALID_HANDLE_VALUE) {
        PrintLastError(TEXT("Error opening pipe"), GetLastError());
        return 1;
    }

    DWORD dwMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(hPipe, &dwMode, NULL, NULL)) {
        PrintLastError(TEXT("SetNamedPipeHandleState failed"), GetLastError());
        CloseHandle(hPipe);
        return 1;
    }

    GameMessage initMsg;
    initMsg.type = MSG_NEW_CLIENT;
    _tcscpy_s(initMsg.clientName, 50, argv[1]);

    HANDLE hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ovWrite = { 0 };
    ovWrite.hEvent = hWriteEvent;
    DWORD cbWritten;

    if (!WriteFile(hPipe, &initMsg, sizeof(GameMessage), &cbWritten, &ovWrite)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            PrintLastError(TEXT("Error sending name"), GetLastError());
            CloseHandle(hPipe);
            return 1;
        }
    }

    WaitForSingleObject(hWriteEvent, INFINITE);
    GetOverlappedResult(hPipe, &ovWrite, &cbWritten, FALSE);
    CloseHandle(hWriteEvent);

    if (cbWritten != sizeof(GameMessage)) {
        _tprintf(TEXT("Failed to register with server\n"));
        CloseHandle(hPipe);
        return 1;
    }

    WaitForSingleObject(hConsoleMutex, INFINITE);
    _tprintf(TEXT("\n[%s] Bot started (reaction time: %ds)"), argv[1], reactionTime);
    ReleaseMutex(hConsoleMutex);


    HANDLE hPipeThread = CreateThread(NULL, 0, PipeReaderThread, hPipe, 0, NULL);
    HANDLE hBehaviorThread = CreateThread(NULL, 0, BotBehaviorThread, &initMsg, 0, NULL);

    WaitForSingleObject(hEvSai, INFINITE);
    DeveContinuar = 0;

    WaitForSingleObject(hPipeThread, 3000);
    WaitForSingleObject(hBehaviorThread, 3000);
    CloseHandle(hPipe);
    CloseHandle(hPipeThread);
    CloseHandle(hBehaviorThread);
    CloseHandle(hConsoleMutex);
    CloseHandle(hEvSai);

    return 0;
}