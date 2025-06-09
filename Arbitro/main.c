#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include "struct.h"

#define BUFSIZE 2048  
#define DEFAULT_MAXLETRAS 6
#define DEFAULT_RITMO 3
#define MAXLETRAS 12
#define REGISTRY_PATH _T("Software\\TrabSO2")
#define MAXCLIENTES 20
#define DEFAULT_REACTION_TIME 5

const TCHAR* validWords[4] = {
    _T("VERMELHO"),
    _T("AZUL"),
    _T("AMARELO"),
    _T("VERDE")
};

// -> Valores no registry
void ValoresRegistry(DWORD* numLetras, DWORD* ritmo);
// -> Letras na memória partilhada:
BOOL initMemAndSync(ServerState* state);
DWORD WINAPI produzLetras(LPVOID p);
// -> Conexão por Named pipes servidor-clientes:
DWORD WINAPI rececaoPipes(LPVOID lpParam);
DWORD WINAPI InstanceThread(LPVOID lpvParam);
// -> Gestão de clientes:
void iniciaClientes(ServerState* state);
int adicionaCliente(ServerState* state, HANDLE hPipe, TCHAR* name);
boolean removeCliente(ServerState* state, HANDLE hPipe);
// -> Envio de mensagens para cliente/clientes:
int broadcastClientes(ServerState* state, GameMessage msg, HANDLE h);
int writeClienteASINC(ServerState* state, HANDLE hPipe, GameMessage msg);
// -> Helpers / Game management
BOOL ProcessGuess(ServerState* state, const TCHAR* guess, const TCHAR* playerName);
int GetPlayerPoints(const ServerState* state, const TCHAR* playerName);
void PrintLastError(TCHAR* part, DWORD id);
void LaunchBot(TCHAR* name, int reactionTime);
BOOL checkScore(ServerState* state);
void UpdateSharedClients(ServerState* state);

typedef struct {
    HANDLE hPipe;
    ServerState* state;
} InstanceThreadParams;

int _tmain(int argc, TCHAR* argv[]) {
    HANDLE hSHM;
    ServerState state = { 0 };
    state.cdata.exit = FALSE;
    state.currentPos = 0; 
    state.cdata.maxPontos = 0;

    state.hStdIn = CreateFile(_T("CONIN$"), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, NULL);
    state.hEvSai = CreateEvent(NULL, TRUE, FALSE, NULL);
    state.hEvIn = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!state.hEvIn) {
        PrintLastError(TEXT("Erro ao criar evento de teclado"), GetLastError());
        return -1;
    }
    state.hWriteReady = CreateEvent(NULL, TRUE, FALSE, NULL);

#ifdef UNICODE 
    _setmode(_fileno(stdin), _O_WTEXT);
    _setmode(_fileno(stdout), _O_WTEXT);
    _setmode(_fileno(stderr), _O_WTEXT);
#endif

    _tprintf(TEXT("> COMANDOS DISPONIVEIS\n >>> 'listar'\n >>> 'excluir [username]'\n >>> 'iniciarbot [username]'\n >>> 'acelerar'\n >>> 'travar'\n >>> 'encerrar'\n\n"));

    iniciaClientes(&state);

    if (!initMemAndSync(&state)) {
        _tprintf(TEXT("Erro ao inicializar memória compartilhada\n"));
        return -1;
    }

    // Thread para produção de letras
    HANDLE hProducerThread = CreateThread(NULL, 0, produzLetras, &state, 0, NULL);

    // Thread para aceitar conexões
    HANDLE hPipeThread = CreateThread(NULL, 0, rececaoPipes, &state, 0, NULL);


    TCHAR buf[256];
    CHAR b[256];
    DWORD n = 0;
    BOOL ret;
    OVERLAPPED ovIn = { 0 };
    ovIn.hEvent = state.hEvIn;

    while (!state.cdata.exit) {
        ResetEvent(state.hEvIn);
        ret = ReadFile(state.hStdIn, b, sizeof(b), &n, &ovIn);

        if (!ret && GetLastError() != ERROR_IO_PENDING) {
            PrintLastError(TEXT("Erro no teclado"), GetLastError());
            break;
        }

        HANDLE h[2] = { state.hEvIn, state.hEvSai };
        DWORD dwWait = WaitForMultipleObjects(2, h, FALSE, INFINITE);

        if (dwWait == WAIT_OBJECT_0) { // Teclado
            GetOverlappedResult(state.hStdIn, &ovIn, &n, FALSE);
            if (n >= 2) b[n - 2] = '\0'; // Remove \r\n

            MultiByteToWideChar(CP_UTF8, 0, b, -1, buf, 256);

            if (_tcscmp(buf, TEXT("exit")) == 0) {
                GameMessage broadCastMsg;
                broadCastMsg.type = MSG_SHUTDOWN;
                HANDLE hPipe = NULL;
                _stprintf_s(broadCastMsg.msg, 100, _T("\n[ARBITRO] Arbitro encerrado\n"));
                broadcastClientes(&state, broadCastMsg, hPipe);
                state.cdata.exit = TRUE;
                SetEvent(state.hEvSai);
                _tprintf(TEXT("\n[SERVIDOR] A encerrar..."));
            }

            else if (_tcscmp(buf, TEXT("lista")) == 0) {
                _tprintf(TEXT("\n--- Lista de Clientes ---"));
                for (int i = 0; i < MAXCLIENTES; i++) {
                    if (state.clientes[i].hPipe != NULL) {
                        _tprintf(TEXT("\nCLIENTE [%d]: %s | Pontos = %d"), i, state.clientes[i].name, state.clientes[i].pontuacao);
                    }
                }
            }

            else if (_tcscmp(buf, TEXT("acelerar")) == 0) {
                state.cdata.shm->ritmo--;
                _tprintf(_T("\nRitmo 1 segundo mais rápido. Ritmo atual = %d s"), state.cdata.shm->ritmo);
            }

            else if (_tcscmp(buf, TEXT("travar")) == 0) {
                state.cdata.shm->ritmo++;
                _tprintf(_T("\nRitmo 1 segundo mais lento. Ritmo atual = %d s"), state.cdata.shm->ritmo);
            }

            else if (_tcsncmp(buf, TEXT("iniciarbot "), 11) == 0) {
                TCHAR* rest = buf + 11;
                TCHAR* name = _tcstok(rest, _T(" "));
                TCHAR* timeStr = _tcstok(NULL, _T(" "));

                if (name && timeStr) {
                    int reactionTime = _ttoi(timeStr);
                    if (reactionTime < 1) reactionTime = DEFAULT_REACTION_TIME;
                    LaunchBot(name, reactionTime);
                }
                else {
                    _tprintf(TEXT("\nUsage: iniciarbot [nome] [tempo de reacao]"));
                }
            }

            else if (_tcsncmp(buf, TEXT("excluir "), 8) == 0) {
                TCHAR username[50];
                _tcscpy_s(username, 50, buf + 8); 
                TCHAR* end = username + _tcslen(username) - 1;
                while (end > username && _istspace(*end)) {
                    *end = '\0';
                    end--;
                }

                // Find the client with matching username
                int clientFound = -1;
                for (int i = 0; i < MAXCLIENTES; i++) {
                    if (state.clientes[i].hPipe != NULL &&
                        _tcscmp(state.clientes[i].name, username) == 0) {
                        clientFound = i;
                        break;
                    }
                }
                if (clientFound != -1) {
                    GameMessage shutdownMsg;
                    shutdownMsg.type = MSG_SHUTDOWN;
                    _tcscpy_s(shutdownMsg.clientName, 50, username);
                    _tcscpy_s(shutdownMsg.msg, 100, _T("\nVocê foi desconectado pelo administrador"));

                    if (writeClienteASINC(&state, state.clientes[clientFound].hPipe, shutdownMsg)) {
                        _tprintf(TEXT("\nEnviado comando de shutdown para %s"), username);

                        
                        removeCliente(&state, state.clientes[clientFound].hPipe);

                        GameMessage broadCastMsg;
                        broadCastMsg.type = MSG_INFO;
                        _stprintf_s(broadCastMsg.msg, 100, _T("\n[INFO] %s saiu do jogo\n"), username);
                        broadcastClientes(&state, broadCastMsg, state.clientes[clientFound].hPipe);
                    }
                    else {
                        _tprintf(TEXT("\nFalha ao enviar comando para %s"), username);
                        // Force cleanup if send failed
                        removeCliente(&state, state.clientes[clientFound].hPipe);
                    }
                }
                else {
                    _tprintf(TEXT("\nCliente '%s' não encontrado"), username);
                }
            }

            else if (_tcscmp(buf, TEXT("encerrar")) == 0) {
                GameMessage shutdownMsg;
                shutdownMsg.type = MSG_SHUTDOWN;
                HANDLE hPipe = NULL;
                _tcscpy_s(shutdownMsg.msg, 100, _T("\nServidor terminado pelo administrador\nA terminar programa..."));
                if (broadcastClientes(&state, shutdownMsg, hPipe)) {
                    _tprintf(TEXT("\nClientes avisados!"));
                }
                state.cdata.exit = TRUE;
                break;
            }
            else {
                _tprintf(TEXT("\nComando desconhecido: %s"), buf);
            }
        }

        else if (dwWait == WAIT_OBJECT_0 + 1) { // Sinal de saída
            break;
        }
    }

    // Limpeza
    state.cdata.exit = TRUE;
    SetEvent(state.hEvSai);
    WaitForSingleObject(hProducerThread, 5000);
    WaitForSingleObject(hPipeThread, 5000);
    CloseHandle(state.hStdIn);
    CloseHandle(state.hEvSai);
    CloseHandle(state.hWriteReady);
    CloseHandle(state.hEvIn);
    UnmapViewOfFile(state.cdata.shm);
    CloseHandle(state.cdata.hMapFile);
    CloseHandle(state.cdata.hMutex);

    return 0;

    //// Checkar se valores no registry estao corretos
    //_tprintf(_T("\nValores finais:\n"));
    //_tprintf(_T("MAXLETRAS = %d\n"), numLetras);
    //_tprintf(_T("RITMO = %d\n"), ritmo);
}


DWORD WINAPI produzLetras(LPVOID p) {
    ServerState* state = (ServerState*)p;
    state->cdata.shm->numLetras = 0;
    state->cdata.shm->ritmo = 0;
    ValoresRegistry(&state->cdata.shm->numLetras, &state->cdata.shm->ritmo);
    srand((unsigned int)time(NULL));

    while (1) {
        if (state->cdata.exit) break;

        if (state->cdata.shm->numClientesAtivos > 1) {
            WaitForSingleObject(state->cdata.hMutex, INFINITE);

            //Limpa array e seleciona nova palavra
            if (_tcslen(state->currentWord) == 0) {
                for (int i = 0; i < state->cdata.shm->numLetras; i++) {
                    state->cdata.shm->buffer[i] = '_';
                }
                state->currentPos = 0;
                state->cdata.shm->writePos = 0;

                int wordCount = sizeof(validWords) / sizeof(validWords[0]);
                if (wordCount > 0) {
                    _tcscpy_s(state->currentWord, 20, validWords[rand() % wordCount]);
                    _tprintf(TEXT("\n[AJUDA] Palavra atual: %s\n"), state->currentWord);
                }
            }

            // Gera letra - para facilitar o jogo, foi definida uma probabilidade de 70% de gerar uma letra da palavra selecionada em vez de ser uma letra totalmente aleatória
            char newLetter;
            if (_tcslen(state->currentWord) > 0 && (rand() % 10) < 7) {
                newLetter = (char)state->currentWord[rand() % _tcslen(state->currentWord)];
            }
            else {
                newLetter = 'A' + (rand() % 26);
            }

            if (state->cdata.shm->numLetras > 0) {
                state->cdata.shm->buffer[state->currentPos] = newLetter;
                state->currentPos = (state->currentPos + 1) % state->cdata.shm->numLetras;
                state->cdata.shm->writePos++;

                /*// Debug do buffer

                _tprintf(TEXT("\n[DEBUG] Buffer: "));
                for (int i = 0; i < state->cdata.shm->numLetras; i++) {
                    _tprintf(TEXT("%c "), state->cdata.shm->buffer[i]);
                }
                _tprintf(TEXT(" | Pos: %d/%d"), state->currentPos, state->cdata.shm->writePos);
                */
            }

            ReleaseMutex(state->cdata.hMutex);
            Sleep(state->cdata.shm->ritmo * 1000);
        }
        else {
            Sleep(1000); //espera jogadores
        }
    }
    return 0;
}

BOOL initMemAndSync(ServerState* state) {
    DWORD shmSize = sizeof(SharedMem) + MAXLETRAS * sizeof(char);
    
    state->cdata.hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);

    if (state->cdata.hMapFile == NULL) {
        state->cdata.hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE,
            0,
            shmSize,  // Use calculated size
            SHM_NAME);

        if (state->cdata.hMapFile == NULL) {
            PrintLastError(TEXT("ERROR: CreateFileMapping"), GetLastError());
            return FALSE;
        }
    }

    state->cdata.shm = (SharedMem*)MapViewOfFile(
        state->cdata.hMapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        shmSize);  // Use same size here

    if (state->cdata.shm == NULL) {
        PrintLastError(TEXT("Error: MapViewOfFile"), GetLastError());
        CloseHandle(state->cdata.hMapFile);
        return FALSE;
    }

    state->cdata.shm->numClientesAtivos = 0;

    state->cdata.shm->writePos = 0;
    state->cdata.shm->readPos = 0;

    for (int i = 0; i < MAXLETRAS; i++) {
        state->cdata.shm->buffer[i] = '_';
    }
        
    state->cdata.hMutex = CreateMutex(NULL, FALSE, MUTEX_SERVER_NAME);
    if (state->cdata.hMutex == NULL) {
        PrintLastError(TEXT("Error: CreateMutex"), GetLastError());
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

    RegCloseKey(hKey);
}




DWORD WINAPI InstanceThread(LPVOID lpvParam) {
    InstanceThreadParams* params = (InstanceThreadParams*)lpvParam;
    HANDLE hPipe = params->hPipe;
    ServerState* state = params->state;
    GameMessage msg, resposta;
    DWORD cbBytesRead = 0;

    HANDLE hReadEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE hWriteEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (!hReadEvent || !hWriteEvent) {
        PrintLastError(TEXT("Error creating events"), GetLastError());
        return -1;
    }

    // Eventos para esperar - leitura ou shutdown
    HANDLE hEvents[2] = { hReadEvent, state->hEvSai };

    BOOL cond = TRUE;
    while (cond) {
        OVERLAPPED ov = { 0 };
        ov.hEvent = hReadEvent;
        ResetEvent(hReadEvent);

        BOOL fSuccess = ReadFile(
            hPipe,
            &msg,
            sizeof(GameMessage),
            &cbBytesRead,
            &ov);

        if (!fSuccess) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                PrintLastError(TEXT("Erro na leitura do pipe"), err);
                break;
            }
        }

        DWORD dwWait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
        if (dwWait == WAIT_OBJECT_0 + 1) { // hEvSai (shutdown)
            cond = FALSE;
            break;
        }
        else if (dwWait != WAIT_OBJECT_0) { 
            _tprintf(TEXT("\nWaitForMultipleObjects retornou %u"), dwWait);
            cond = FALSE;
            break;
        }

        if (!GetOverlappedResult(hPipe, &ov, &cbBytesRead, FALSE)) {
            PrintLastError(TEXT("Erro no GetOverlappedResult"), GetLastError());
            removeCliente(state, hPipe);
            break;
        }

        if (cbBytesRead != sizeof(GameMessage)) {
            _tprintf(TEXT("\nMensagem incompleta recebida (%u bytes)"), cbBytesRead);
            continue;
        }

        switch (msg.type) {
        case MSG_NEW_CLIENT:
            _tprintf(TEXT("\nNova conexão estabelecida! Nome do Cliente: [%s]\n"), msg.clientName);
            int res = adicionaCliente(state, hPipe, msg.clientName);

            ZeroMemory(&resposta, sizeof(GameMessage));
            resposta.name_feedback = (res == 1); // 1 sucesso, 0 falha

            if (res == -1) { // Nome duplicado
                resposta.type = MSG_NAME_FEEDBACK;
                _tcscpy_s(resposta.msg, 100, _T("\n[ARBITRO] Nome já existe\n"));
                _tprintf(TEXT("\nConexão rejeitada - nome duplicado: %s"), msg.clientName);
            }
            else if (res == 0) { // Servidor cheio
                resposta.type = MSG_NAME_FEEDBACK;
                _tcscpy_s(resposta.msg, 100, _T("\n[ARBITRO] Servidor cheio\n"));
                _tprintf(TEXT("\nConexão rejeitada - servidor cheio\n"));
            }
            else { // Sucesso
                resposta.type = MSG_NAME_FEEDBACK;
                _stprintf_s(resposta.msg, 100, _T("\n[ARBITRO] Bem-vindo %s\n"), msg.clientName);

                // Avisar outros clientes
                GameMessage broadcastMsg;
                broadcastMsg.type = MSG_INFO;
                _stprintf_s(broadcastMsg.msg, 100, _T("\n[INFO] Novo jogador: %s\n"), msg.clientName);
                broadcastClientes(state, broadcastMsg, hPipe);
            }

            writeClienteASINC(state, hPipe, resposta);

            if (res != 1) { // Fecha conexao em caso de insucesso
                cond = FALSE;
            }
            break;


        case MSG_WORD_GUESS:
            _tprintf(TEXT("\nPalpite de %s: %s\n"), msg.clientName, msg.wordGuess);

            resposta.type = MSG_WORD_FEEDBACK;
            resposta.correct = ProcessGuess(state, msg.wordGuess, msg.clientName); 
            resposta.points = GetPlayerPoints(state, msg.clientName); 

            if (resposta.correct) {
                _stprintf_s(resposta.msg, 100, _T("\n[ARBITRO] Palpite correto! Pontos atuais = %d pontos\n"), resposta.points);
            }
            else {
                _stprintf_s(resposta.msg, 100, _T("\n[ARBITRO] Palpite errado! Pontos atuais = %d pontos. Tente novamente\n"), resposta.points);
            }

            writeClienteASINC(state, hPipe, resposta);

            //informar aos restantes jogadores que alguem acertou
            if (resposta.correct) {
                GameMessage updateMsg;
                updateMsg.type = MSG_INFO;
                _stprintf_s(updateMsg.msg, 100, _T("[INFO] %s acertou!\n"), msg.clientName);
                broadcastClientes(state, updateMsg, hPipe);
            }

            checkScore(state);
            break;


        case MSG_PONT_REQUEST:
            resposta.type = MSG_INFO;
            int pontos = GetPlayerPoints(state, msg.clientName);
            _stprintf_s(resposta.msg, 100, _T("\n[ARBITRO] Sua pontuação: %d pontos\n"), pontos);

            writeClienteASINC(state, hPipe, resposta);
  
            break;


        case MSG_LIST_REQUEST:
            resposta.type = MSG_INFO;
            TCHAR temp[100] = { 0 };
            _tcscpy_s(resposta.msg, 100, _T("\n[ARBITRO] Jogadores conectados:\n"));

            for (int i = 0; i < MAXCLIENTES; i++) {
                if (state->clientes[i].hPipe != NULL) {
                    _stprintf_s(temp, 100, _T("%d. %s (%d pts)\n"),
                        i + 1, state->clientes[i].name, state->clientes[i].pontuacao);
                    _tcscat_s(resposta.msg, 100, temp);
                }
            }
            writeClienteASINC(state, hPipe, resposta);

            break;


        case MSG_EXIT:
            _tprintf(TEXT("\n[INFO] Cliente %s saiu\n"), msg.clientName);
            removeCliente(state, hPipe);

            //Avisar restantes jogadores quem saiu
            GameMessage exitMsg;
            exitMsg.type = MSG_INFO;
            _stprintf_s(exitMsg.msg, 100, _T("\n[INFO] %s saiu do jogo\n"), msg.clientName);
            broadcastClientes(state, exitMsg, hPipe);
            cond = FALSE;
            break;


        default:
            _tprintf(TEXT("\nTipo de mensagem desconhecido: %d\n"), msg.type);
            resposta.type = MSG_INFO;
            _tcscpy_s(resposta.msg, 100, _T("[ARBITRO] Comando desconhecido"));
            writeClienteASINC(state, hPipe, resposta);
            break;

        }
    }

    removeCliente(state, hPipe);
    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    CloseHandle(hReadEvent);
    CloseHandle(hWriteEvent);
    free(params);

    return 1;
}

DWORD WINAPI rececaoPipes(LPVOID lpParam) {
    ServerState* state = (ServerState*)lpParam;
    LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\tpSO2");

    while (!state->cdata.exit) {
        HANDLE hPipe = CreateNamedPipe(
            lpszPipename,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            BUFSIZE,
            BUFSIZE,
            5000,
            NULL);

        if (hPipe == INVALID_HANDLE_VALUE) {
            if (!state->cdata.exit) PrintLastError(TEXT("Erro ao criar pipe"), GetLastError());
            break;
        }

        _tprintf(TEXT("[Aguardando conexão...]\n"));
        BOOL fConnected = ConnectNamedPipe(hPipe, NULL) ?
            TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (fConnected && !state->cdata.exit) {
            InstanceThreadParams* params = malloc(sizeof(InstanceThreadParams));
            if (!params) {
                PrintLastError(TEXT("Error allocating thread params"), GetLastError());
                CloseHandle(hPipe);
                continue;
            }
            params->hPipe = hPipe;
            params->state = state;

            HANDLE hClientThread = CreateThread(NULL, 0, InstanceThread, params, 0, NULL);
            if (!hClientThread) {
                PrintLastError(TEXT("Erro ao criar thread de cliente"), GetLastError());
                CloseHandle(hPipe);
                free(params);
            }
            else {
                CloseHandle(hClientThread);
            }
        }
        else {
            CloseHandle(hPipe);
        }

        // Verificar se deve sair
        if (WaitForSingleObject(state->hEvSai, 0) == WAIT_OBJECT_0) {
            break;
        }
    }

    return 0;
}

int GetPlayerPoints(const ServerState* state, const TCHAR* playerName) {
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL &&
            _tcscmp(playerName, state->clientes[i].name) == 0) {
            return state->clientes[i].pontuacao;
        }
    }
    return -1;
}


BOOL ProcessGuess(ServerState* state, const TCHAR* guess, const TCHAR* playerName) {
    TCHAR upperGuess[20];
    _tcscpy_s(upperGuess, 20, guess);
    _tcsupr_s(upperGuess, 20);

    //limpeza
    size_t len = _tcslen(upperGuess);
    while (len > 0 && _istspace(upperGuess[len - 1])) {
        upperGuess[--len] = '\0';
    }

    //verificar se guess faz parte do dicionario
    BOOL isValidWord = FALSE;
    for (int i = 0; validWords[i] != NULL; i++) {
        if (_tcscmp(upperGuess, validWords[i]) == 0) {
            isValidWord = TRUE;
            break;
        }
    }

    // Para facilitar o jogo, foi definido que basta metade das letras deem match para considerar certo
    int requiredMatches = (_tcslen(upperGuess) + 1) / 2; 
    int actualMatches = 0;
    TCHAR matchedLetters[20] = { 0 };

    for (size_t i = 0; i < _tcslen(upperGuess); i++) {
        for (unsigned int j = 0; j < state->cdata.shm->writePos; j++) {
            if (toupper(state->cdata.shm->buffer[j]) == upperGuess[i]) {
                actualMatches++;
                matchedLetters[_tcslen(matchedLetters)] = upperGuess[i];
                matchedLetters[_tcslen(matchedLetters) + 1] = '\0';
                break;
            }
        }
    }

    int playerIndex = -1;
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL && _tcscmp(state->clientes[i].name, playerName) == 0) {
            playerIndex = i;
            break;
        }
    }

    if (playerIndex == -1) {
        return FALSE;
    }

    int wordLength = (int)_tcslen(upperGuess);

    if (actualMatches >= requiredMatches && isValidWord) {
        // 1 ponto por letra certa
        int pointsEarned = wordLength;
        WaitForSingleObject(state->cdata.hMutex, INFINITE);
        state->clientes[playerIndex].pontuacao += pointsEarned;

        // Limpar o buffer 
        _tcscpy_s(state->currentWord, 20, _T(""));
        for (unsigned int i = 0; i < MAXLETRAS; i++) {
            state->cdata.shm->buffer[i] = '_';
        }
        state->cdata.shm->writePos = 0;
        state->currentPos = 0;

        _tprintf(TEXT("\n%s acertou! +%d pontos (Acertou %d/%d letras: %s)"),
            playerName, pointsEarned, actualMatches, requiredMatches, matchedLetters);
        ReleaseMutex(state->cdata.hMutex);
        UpdateSharedClients(state);
        return TRUE;
    }
    else {
        // palavra errada - tirar 0.5 pontos por letra
        WaitForSingleObject(state->cdata.hMutex, INFINITE);
        float pointsLost = wordLength * 0.5f;
        state->clientes[playerIndex].pontuacao -= (int)pointsLost;

        _tprintf(TEXT("\n%s errou! -%.1f pontos (Matched %d/%d letters: %s)"),
            playerName, pointsLost, actualMatches, requiredMatches, matchedLetters);
        ReleaseMutex(state->cdata.hMutex);
        UpdateSharedClients(state);
        return FALSE;
    }
}

// ------------------ TRATAMENTO DE CLIENTES E COMUNICAÇÃO BROADCAST ------------------ 
void iniciaClientes(ServerState* state) {
    for (int i = 0; i < MAXCLIENTES; i++) {
        WaitForSingleObject(state->cdata.hMutex, INFINITE);
        
        state->clientes[i].hPipe = NULL;
        state->clientes[i].name[0] = TEXT('\0');
        state->clientes[i].pontuacao = 0;
        
        ReleaseMutex(state->cdata.hMutex);
    }
}

int adicionaCliente(ServerState* state, HANDLE hPipe, TCHAR* name) {
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL &&
            _tcscmp(state->clientes[i].name, name) == 0) {
            return -1; // Nome já existe
        }
    }

    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe == NULL) {
            WaitForSingleObject(state->cdata.hMutex, INFINITE);

            state->clientes[i].hPipe = hPipe;
            _tcscpy_s(state->clientes[i].name, 50, name);
            state->clientes[i].pontuacao = 0;
            (state->cdata.shm->numClientesAtivos)++;

            ReleaseMutex(state->cdata.hMutex);
            UpdateSharedClients(state);
            return 1;
        }
    }

    return 0; // Sem slots disponíveis
}

boolean removeCliente(ServerState* state, HANDLE hPipe) {
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe == hPipe) {
            WaitForSingleObject(state->cdata.hMutex, INFINITE);

            state->clientes[i].hPipe = NULL;
            state->clientes[i].name[0] = TEXT('\0');
            (state->cdata.shm->numClientesAtivos)--;

            ReleaseMutex(state->cdata.hMutex);
            UpdateSharedClients(state);
            return 1;
        }
    }
    return 0;
}

int writeClienteASINC(ServerState* state, HANDLE hPipe, GameMessage msg) {
    DWORD cbWritten = 0;
    OVERLAPPED OverlWr = { 0 };
    OverlWr.hEvent = state->hWriteReady;
    ResetEvent(state->hWriteReady);

    BOOL fSuccess = WriteFile(
        hPipe,
        &msg,
        sizeof(GameMessage),
        &cbWritten,
        &OverlWr);

    WaitForSingleObject(state->hWriteReady, INFINITE);
    GetOverlappedResult(hPipe, &OverlWr, &cbWritten, FALSE);

    return (cbWritten == sizeof(GameMessage)) ? 1 : 0;
}


int broadcastClientes(ServerState* state, GameMessage msg, HANDLE h) {
    int numwrites = 0;
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL && state->clientes[i].hPipe != h) {
            if (writeClienteASINC(state, state->clientes[i].hPipe, msg)) {
                ++numwrites;
            }
           else {
                removeCliente(state, state->clientes[i].hPipe);
           }
        }
    }
    return numwrites;
}
// ------------------------------------------------------------------------------------


void PrintLastError(TCHAR* part, DWORD id) {
    LPTSTR buffer;
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



void LaunchBot(TCHAR* name, int reactionTime) {
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi;
    TCHAR cmdLine[256];

    _stprintf_s(cmdLine, 256, _T("bot.exe %s %d"), name, reactionTime);

    if (!CreateProcess(
        NULL,
        cmdLine,
        NULL,
        NULL,
        FALSE,
        CREATE_NEW_CONSOLE,
        NULL,
        NULL,
        &si,
        &pi)) {
        PrintLastError(TEXT("Error launching bot"), GetLastError());
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    _tprintf(TEXT("\n[INFO] Bot '%s' launched with reaction time %ds"), name, reactionTime);
}

BOOL checkScore(ServerState* state) {
    int currentMax = 0;
    int currentLeaderIndex = -1;
    static TCHAR lastLeaderName[50] = { 0 };  // Mantém o nome do último líder

    // Encontra a pontuação máxima atual e o líder
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL &&
            state->clientes[i].pontuacao > currentMax) {
            currentMax = state->clientes[i].pontuacao;
            currentLeaderIndex = i;
        }
    }

    // Verifica se houve mudança na liderança
    if (currentLeaderIndex != -1) {
        BOOL newLeader = (_tcscmp(lastLeaderName, state->clientes[currentLeaderIndex].name) != 0);
        BOOL higherScore = (currentMax > state->cdata.maxPontos);

        if (newLeader || (higherScore && currentLeaderIndex == 0)) {
            // Atualiza o registo do líder
            _tcscpy_s(lastLeaderName, 50, state->clientes[currentLeaderIndex].name);
            state->cdata.maxPontos = currentMax;

            // Notifica o novo líder
            GameMessage msgCli;
            msgCli.type = MSG_INFO;

            if (state->cdata.maxPontos == currentMax) {  // Primeira vez
                _stprintf_s(msgCli.msg, 100,
                    _T("\n[ARBITRO] És o novo líder com %d pontos!\n"),
                    state->cdata.maxPontos);
            }
            else {
                _stprintf_s(msgCli.msg, 100,
                    _T("\n[ARBITRO] Parabéns! Assumiste a liderança com %d pontos!\n"),
                    state->cdata.maxPontos);
            }

            writeClienteASINC(state, state->clientes[currentLeaderIndex].hPipe, msgCli);

            // Notifica outros jogadores
            GameMessage msgBroadcast;
            msgBroadcast.type = MSG_INFO;
            _stprintf_s(msgBroadcast.msg, 100,
                _T("\n[INFO] %s é o novo líder com %d pontos!\n"),
                state->clientes[currentLeaderIndex].name,
                state->cdata.maxPontos);

            broadcastClientes(state, msgBroadcast, state->clientes[currentLeaderIndex].hPipe);
            return TRUE;
        }
    }

    return FALSE;
}

void UpdateSharedClients(ServerState* state) {
    WaitForSingleObject(state->cdata.hMutex, INFINITE);

    for (int i = 0; i < MAXCLIENTES; i++) {
        state->cdata.shm->clients[i].name[0] = '\0';
        state->cdata.shm->clients[i].points = 0;
    }

    // Copiar apenas os clientes ativos para as primeiras posições
    int activeCount = 0;
    for (int i = 0; i < MAXCLIENTES; i++) {
        if (state->clientes[i].hPipe != NULL) {
            _tcscpy_s(state->cdata.shm->clients[activeCount].name, 50,
                state->clientes[i].name);
            state->cdata.shm->clients[activeCount].points = state->clientes[i].pontuacao;
            activeCount++;
        }
    }

    //Ordenar
    for (int i = 1; i < activeCount; i++) {
        SharedClientInfo current = state->cdata.shm->clients[i];
        int j = i - 1;

        while (j >= 0 && state->cdata.shm->clients[j].points < current.points) {
            state->cdata.shm->clients[j + 1] = state->cdata.shm->clients[j];
            j--;
        }
        state->cdata.shm->clients[j + 1] = current;
    }

    ReleaseMutex(state->cdata.hMutex);

    // Debug output
    /*_tprintf(TEXT("\n--- [DEBUG SHM] Sorted by points ---"));
    for (int i = 0; i < activeCount; i++) {
        _tprintf(TEXT("\nCLIENTE [%d]: %s | Pontos = %d"),
            i,
            state->cdata.shm->clients[i].name,
            state->cdata.shm->clients[i].points);
    }*/
}