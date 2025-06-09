#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include "struct.h"
#define DEFAULT_MAXLETRAS 6
#define DEFAULT_RITMO 3
#define MAXLETRAS 12
#define REGISTRY_PATH _T("Software\\TrabSO2")
#define BUFSIZE 2048  

DWORD WINAPI consomeLetras(LPVOID p);
DWORD WINAPI PipeReaderThread(LPVOID lpvParam);
BOOL initMemAndSync(ClientState* state);
void ValoresRegistry(DWORD* numLetras, DWORD* ritmo);
void PrintLastError(TCHAR* part, DWORD id);

int _tmain(int argc, TCHAR* argv[]) {
    if (argc < 2) {
        _tprintf(TEXT("Usage: %s <UserName>\n"), argv[0]);
        return 1;
    }

#ifdef UNICODE 
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif

    _tprintf(TEXT("\n> COMANDOS DISPONIVEIS:\n >>> ':pont'\n >>> ':jogs'\n >>> ':sair'\n"));

    ClientState state;
    ZeroMemory(&state, sizeof(ClientState)); 
    state.DeveContinuar = 1; 

    state.hConsoleMutex = CreateMutex(NULL, FALSE, NULL);
    if (!state.hConsoleMutex) {
        PrintLastError(TEXT("Error creating console mutex"), GetLastError());
        return -1;
    }

    state.hEvSai = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!state.hEvSai) {
        PrintLastError(TEXT("Error creating shutdown event"), GetLastError());
        return -1;
    }

    // Conectar ao pipe
    LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\tpSO2");
    const int maxAttempts = 5;
    int attempts = 0;
    state.hPipe = NULL;

    while (attempts < maxAttempts) {
        state.hPipe = CreateFile(
            lpszPipename,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);

        if (state.hPipe != INVALID_HANDLE_VALUE) break;

        if (GetLastError() != ERROR_PIPE_BUSY) {
            PrintLastError(TEXT("Error opening pipe"), GetLastError());
            return -1;
        }

        attempts++;
        Sleep(10000);
    }

    if (state.hPipe == INVALID_HANDLE_VALUE) {
        _tprintf(TEXT("Could not connect to server after %d attempts.\n"), maxAttempts);
        return -1;
    }

    
    DWORD dwMode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(state.hPipe, &dwMode, NULL, NULL)) {
        PrintLastError(TEXT("SetNamedPipeHandleState failed"), GetLastError());
        CloseHandle(state.hPipe);
        return -1;
    }

    // Registar cliente
    GameMessage initMsg;
    initMsg.type = MSG_NEW_CLIENT;
    _tcscpy_s(initMsg.clientName, 50, argv[1]);

    HANDLE hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ovWrite = { 0 };
    ovWrite.hEvent = hWriteEvent;
    DWORD cbWritten;

    if (!WriteFile(state.hPipe, &initMsg, GmMsg_size, &cbWritten, &ovWrite)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            PrintLastError(TEXT("Error sending name"), GetLastError());
            CloseHandle(state.hPipe);
            return -1;
        }
    }

    WaitForSingleObject(hWriteEvent, INFINITE);
    GetOverlappedResult(state.hPipe, &ovWrite, &cbWritten, FALSE);
    CloseHandle(hWriteEvent);

    if (cbWritten != GmMsg_size) {
        _tprintf(TEXT("Failed to send client name\n"));
        CloseHandle(state.hPipe);
        return -1;
    }

    state.cdata.exit = FALSE;
    if (!initMemAndSync(&state)) {
        _tprintf(TEXT("Error initializing shared memory\n"));
        CloseHandle(state.hPipe);
        return -1;
    }
   
    HANDLE hPipeThread = CreateThread(NULL, 0, PipeReaderThread, &state, 0, NULL);
    HANDLE hSharedMemThread = CreateThread(NULL, 0, consomeLetras, &state, 0, NULL);

    if (!hPipeThread || !hSharedMemThread) {
        PrintLastError(TEXT("Error creating threads"), GetLastError());
        CloseHandle(state.hPipe);
        return -1;
    }

 
    HANDLE hStdIn = CreateFile(_T("CONIN$"),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,  
        NULL);

    if (hStdIn == INVALID_HANDLE_VALUE) {
        PrintLastError(TEXT("Error getting console input handle"), GetLastError());
        return -1;
    }
    HANDLE hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ovRead = { 0 };
    ovRead.hEvent = hReadEvent;

    TCHAR inputBuffer[256];
    CHAR b[256];  // For raw input before conversion
    DWORD n = 0;
    BOOL ret;

    while (state.DeveContinuar) {
        WaitForSingleObject(state.hConsoleMutex, INFINITE);
        ReleaseMutex(state.hConsoleMutex);

        ResetEvent(hReadEvent);
        ret = ReadFile(hStdIn, b, sizeof(b), &n, &ovRead);

        if (!ret && GetLastError() != ERROR_IO_PENDING) {
            PrintLastError(TEXT("Error reading keyboard"), GetLastError());
            break;
        }

        HANDLE handles[2] = { hReadEvent, state.hEvSai };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) { // Teclado
            GetOverlappedResult(hStdIn, &ovRead, &n, FALSE);
            if (n >= 2) b[n - 2] = '\0'; // Remove \r\n

            MultiByteToWideChar(CP_UTF8, 0, b, -1, inputBuffer, 256);

            GameMessage msgToSend;
            _tcscpy_s(msgToSend.clientName, 50, argv[1]); 

            // Comandos
            if (inputBuffer[0] == TEXT(':')) {
                if (_tcscmp(inputBuffer, TEXT(":pont")) == 0) {
                    msgToSend.type = MSG_PONT_REQUEST;

                   
                    _tprintf(TEXT("\nRequesting points...")); 
                }
                else if (_tcscmp(inputBuffer, TEXT(":jogs")) == 0) {
                    msgToSend.type = MSG_LIST_REQUEST;

                    
                    _tprintf(TEXT("\nRequesting player list..."));
                }
                else if (_tcscmp(inputBuffer, TEXT(":sair")) == 0) {
                    msgToSend.type = MSG_EXIT;

                    
                    _tprintf(TEXT("\nExiting game..."));
                    state.DeveContinuar = 0;
                }
                else {
                    
                    _tprintf(TEXT("\nUnknown command: %s"), inputBuffer);
                    continue;
                }
            }
            else { //Tentativa de adivinhar palavra
                msgToSend.type = MSG_WORD_GUESS;
                _tcscpy_s(msgToSend.wordGuess, 20, inputBuffer);
            }

            HANDLE hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            OVERLAPPED ovWrite = { 0 };
            ovWrite.hEvent = hWriteEvent;
            DWORD cbWritten;

            if (!WriteFile(state.hPipe, &msgToSend, GmMsg_size, &cbWritten, &ovWrite)) {
                if (GetLastError() != ERROR_IO_PENDING) {
                    PrintLastError(TEXT("Error sending message"), GetLastError());
                    CloseHandle(hWriteEvent);
                    break;
                }
                WaitForSingleObject(hWriteEvent, INFINITE);
            }
            GetOverlappedResult(state.hPipe, &ovWrite, &cbWritten, FALSE);
            CloseHandle(hWriteEvent);

            if (cbWritten != GmMsg_size) {
                _tprintf(TEXT("\nFailed to send complete message"));
            }

            if (msgToSend.type == MSG_EXIT) {
                SetEvent(state.hEvSai);
                break;
            }
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) { 
            break;
        }
    }

    state.cdata.exit = TRUE;
    SetEvent(state.hEvSai);
    WaitForSingleObject(hPipeThread, INFINITE);
    WaitForSingleObject(hSharedMemThread, INFINITE);
    CloseHandle(state.hPipe);
    CloseHandle(hPipeThread);
    CloseHandle(hSharedMemThread);
    CloseHandle(hReadEvent);
    CloseHandle(state.hEvSai);
    UnmapViewOfFile(state.cdata.shm);
    CloseHandle(state.cdata.hMapFile);
    CloseHandle(state.cdata.hMutex);

    return 0;
}

DWORD WINAPI consomeLetras(LPVOID p) {
    ClientState* state = (ClientState*)p;
    ControlData* cdata = &state->cdata;
    ValoresRegistry(&cdata->shm->numLetras, &cdata->shm->ritmo);

    if (cdata->shm == NULL) {
        _tprintf(TEXT("Memória compartilhada não inicializada corretamente.\n"));
        return 1;
    }

    while (1) {
        if (cdata->exit == TRUE) {
            return 0; 
        }
        if (cdata->shm->numClientesAtivos > 1) {
            WaitForSingleObject(cdata->hMutex, INFINITE);

            _tprintf(TEXT("LETRAS: "));
            for (DWORD i = 0; i < cdata->shm->numLetras; i++) {
                _tprintf(TEXT("%c "), cdata->shm->buffer[i]);
            }
            _tprintf(TEXT("\n"));

            ReleaseMutex(cdata->hMutex);

            Sleep(cdata->shm->ritmo * 1000);
        }
    }
}

BOOL initMemAndSync(ClientState* state) {
    BOOL firstProcess = FALSE;
    state->cdata.hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);

    if (state->cdata.hMapFile == NULL) {
        state->cdata.hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE, 0, sizeof(SharedMem), SHM_NAME);

        firstProcess = TRUE;
        if (state->cdata.hMapFile == NULL) {
            _tprintf(TEXT("ERROR: CreateFileMapping", GetLastError()));
            return FALSE;
        }
    }

    state->cdata.shm = (SharedMem*)MapViewOfFile(state->cdata.hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMem));
    if (state->cdata.shm == NULL) {
        _tprintf(TEXT("Error: MapViewOfFile (%d)\n"), GetLastError());
        CloseHandle(state->cdata.hMapFile);
        return FALSE;
    }

    if (firstProcess) {
        state->cdata.shm->writePos = 0;
        state->cdata.shm->readPos = 0;
    }

    state->cdata.hMutex = CreateMutex(NULL, FALSE, MUTEX_SERVER_NAME);

    if (state->cdata.hMutex == NULL) {
        _tprintf(TEXT("Error: CreateMutex (%d)\n"), GetLastError());
        UnmapViewOfFile(state->cdata.shm);
        CloseHandle(state->cdata.hMapFile);
        return FALSE;
    }


    return TRUE;
}

void ValoresRegistry(DWORD* numLetras, DWORD* ritmo) {
    HKEY hKey = NULL;
    DWORD dwDisposition;
    DWORD dwType;
    DWORD dwSize = sizeof(DWORD);
    LONG res;

    // Abrir ou criar a chave do registry
    res = RegCreateKeyEx(HKEY_CURRENT_USER,
        REGISTRY_PATH,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS,
        NULL,
        &hKey,
        &dwDisposition);

    if (res != ERROR_SUCCESS) {
        _tprintf(_T("Erro ao acessar o registry: %d\n"), res);
        return;
    }

    // Tentar ler MAXLETRAS do registry
    dwSize = sizeof(DWORD);
    res = RegQueryValueEx(hKey, _T("MAXLETRAS"), NULL, &dwType, (LPBYTE)numLetras, &dwSize);

    if (res != ERROR_SUCCESS || dwType != REG_DWORD) {
        *numLetras = DEFAULT_MAXLETRAS;
        RegSetValueEx(hKey, _T("MAXLETRAS"), 0, REG_DWORD, (const BYTE*)numLetras, sizeof(*numLetras));
        _tprintf(_T("MAXLETRAS não encontrado. Valor padrão %d definido.\n"), *numLetras);
    }

    // Verificar se MAXLETRAS não excede o máximo permitido
    if (*numLetras > MAXLETRAS) {
        *numLetras = MAXLETRAS;
        RegSetValueEx(hKey, _T("MAXLETRAS"), 0, REG_DWORD,
            (const BYTE*)numLetras, sizeof(*numLetras));
        _tprintf(_T("MAXLETRAS ajustado para o máximo permitido: %d\n"), *numLetras);
    }

    // Tentar ler RITMO do registry
    dwSize = sizeof(DWORD);
    res = RegQueryValueEx(hKey, _T("RITMO"), NULL, &dwType,
        (LPBYTE)ritmo, &dwSize);

    if (res != ERROR_SUCCESS || dwType != REG_DWORD) {
        // Se não existir ou tipo incorreto, criar com valor padrão
        *ritmo = DEFAULT_RITMO;
        RegSetValueEx(hKey, _T("RITMO"), 0, REG_DWORD,
            (const BYTE*)ritmo, sizeof(*ritmo));
        _tprintf(_T("RITMO não encontrado ou inválido. Valor padrão %d definido.\n"), *ritmo);
    }

    // Fechar a chave do registry
    RegCloseKey(hKey);
}



DWORD WINAPI PipeReaderThread(LPVOID lpvParam) {
    ClientState* state = (ClientState*)lpvParam;
    HANDLE hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ov = { 0 };
    ov.hEvent = hReadEvent;

    GameMessage serverMsg;
    DWORD bytesRead;

    while (state->DeveContinuar) {
        ResetEvent(hReadEvent);
        if (!ReadFile(state->hPipe, &serverMsg, GmMsg_size, &bytesRead, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                PrintLastError(TEXT("Error reading from pipe"), GetLastError());
                break;
            }
        }

        HANDLE handles[2] = { hReadEvent, state->hEvSai };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0) { // Message received
            GetOverlappedResult(state->hPipe, &ov, &bytesRead, FALSE);

            if (bytesRead != GmMsg_size) {
                _tprintf(TEXT("\nIncomplete message received from server"));
                continue;
            }

            switch (serverMsg.type) {
            case MSG_NAME_FEEDBACK:
                if (!serverMsg.name_feedback) {
                    _tprintf(TEXT("%s"), serverMsg.msg);
                    state->DeveContinuar = 0;
                    SetEvent(state->hEvSai);
                }
                _tprintf(TEXT("%s"), serverMsg.msg);
                break;

            case MSG_INFO:
                _tprintf(TEXT("%s"), serverMsg.msg); 
                break;

            case MSG_WORD_FEEDBACK:
                _tprintf(TEXT("%s"), serverMsg.msg);
                break;

            case MSG_SHUTDOWN:
                _tprintf(TEXT("%s"), serverMsg.msg);
                state->DeveContinuar = 0;
                SetEvent(state->hEvSai);
            break;

            default:
                _tprintf(TEXT("\nUnknown message received from server"));
                break;
            }
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) { // Shutdown signal
            break;
        }
    }

    CloseHandle(hReadEvent);
    return 0;
}


void PrintLastError(TCHAR* part, DWORD id) {
    LPTSTR buffer;  // auto alocado
    if (part == NULL)
        part = TEXT("*");
    if (id == 0)
        id = GetLastError();
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
    _tprintf(TEXT("\n%s Erro %d: %s\n"), part, id, buffer);
    LocalFree(buffer);
}