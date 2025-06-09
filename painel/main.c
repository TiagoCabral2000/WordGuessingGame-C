#include <windows.h>
#include <windowsx.h>
#include <tchar.h>
#include "struct.h"
#define IDC_EDIT_VALUE 2001
#define MAXLETRAS 12
#define MAXCLIENTES 20
#define ID_MENU_VISUALIZACAO   1001
#define ID_MENU_SAIR           1002
#define ID_MENU_SOBRE          1003
#define IDC_EDIT_VALUE 2001
int g_maxJogadores = 5;

TCHAR szProgName[] = TEXT("GameMonitor");

// Function prototypes
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitSharedMemory(ControlData* cdata);
void CleanupSharedMemory(ControlData* cdata);
INT_PTR CALLBACK InputDialogProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI _tWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPTSTR lpCmdLine, int nCmdShow) {
    HWND hWnd;
    MSG msg;
    WNDCLASSEX wc = { 0 };
    ControlData cdata = { 0 };

    // Initialize shared memory
    if (!InitSharedMemory(&cdata)) {
        MessageBox(NULL, TEXT("Failed to access shared memory"), TEXT("Error"), MB_ICONERROR);
        return 1;
    }

    // Register window class
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = szProgName;

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed"), TEXT("Error"), MB_ICONERROR);
        return 1;
    }

    // Create window
    hWnd = CreateWindow(
        szProgName,
        TEXT("Game Monitor - Visualizador de Estado do Jogo"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        NULL, NULL, hInst, &cdata);

    if (!hWnd) {
        MessageBox(NULL, TEXT("Window Creation Failed"), TEXT("Error"), MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    HMENU hMenu = CreateMenu();
    HMENU hSubMenu = CreatePopupMenu();

    AppendMenu(hSubMenu, MF_STRING, ID_MENU_VISUALIZACAO, TEXT("Redefinir visualização"));
    AppendMenu(hSubMenu, MF_STRING, ID_MENU_SAIR, TEXT("Sair"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, TEXT("Opções"));
    AppendMenu(hMenu, MF_STRING, ID_MENU_SOBRE, TEXT("Sobre"));

    SetMenu(hWnd, hMenu);

    // Main message loop
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupSharedMemory(&cdata);
    return (int)msg.wParam;
}

// Input dialog procedure
INT_PTR CALLBACK InputDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int* pValue;

    switch (message) {
    case WM_INITDIALOG:
        pValue = (int*)lParam;
        SetDlgItemInt(hDlg, IDC_EDIT_VALUE, *pValue, FALSE);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            BOOL success;
            int newValue = GetDlgItemInt(hDlg, IDC_EDIT_VALUE, &success, FALSE);
            if (success && newValue >= 1 && newValue <= MAXCLIENTES) {
                *pValue = newValue;
                EndDialog(hDlg, IDOK);
            }
            else {
                MessageBox(hDlg, TEXT("Por favor, insira um número entre 1 e 20"), TEXT("Valor inválido"), MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static ControlData* cdata;

    switch (message) {
    case WM_CREATE:
        cdata = (ControlData*)((LPCREATESTRUCT)lParam)->lpCreateParams;
        SetTimer(hWnd, 1, 1000, NULL);
        break;

    case WM_TIMER:
        InvalidateRect(hWnd, NULL, TRUE);
        UpdateWindow(hWnd);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect;
        GetClientRect(hWnd, &rect);

        HFONT hFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, TEXT("Arial"));
        HFONT hFontSmall = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, TEXT("Arial"));

        SelectObject(hdc, hFont);

        WaitForSingleObject(cdata->hMutex, INFINITE);

        // Draw current letters
        TextOut(hdc, 20, 60, TEXT("Letras visíveis:"), lstrlen(TEXT("Letras visíveis:")));

        TCHAR letters[MAXLETRAS * 2 + 1] = { 0 };
        int len = 0;
        int numToPrint = min(cdata->shm->numLetras, MAXLETRAS);

        for (int i = 0; i < numToPrint && len < _countof(letters) - 2; i++) {
            len += _sntprintf_s(letters + len, _countof(letters) - len, _TRUNCATE, TEXT("%c "), cdata->shm->buffer[i]);
        }

        TextOut(hdc, 300, 60, letters, lstrlen(letters));

        // Draw player list
        SelectObject(hdc, hFontSmall);
        TextOut(hdc, 20, 100, TEXT("Jogadores e Pontuações:"), lstrlen(TEXT("Jogadores e Pontuações:")));

        int yPos = 130;
        for (int i = 0; i < MAXCLIENTES && i < g_maxJogadores; i++) {
            if (cdata->shm->clients[i].name[0] != '\0') {
                TCHAR playerInfo[100];
                _sntprintf_s(playerInfo, _countof(playerInfo), _TRUNCATE,
                    TEXT("%s: %d pontos"),
                    cdata->shm->clients[i].name,
                    cdata->shm->clients[i].points);

                TextOut(hdc, 40, yPos, playerInfo, lstrlen(playerInfo));
                yPos += 25;
            }
        }

        ReleaseMutex(cdata->hMutex);

        DeleteObject(hFont);
        DeleteObject(hFontSmall);
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_MENU_VISUALIZACAO:
            break;

        case ID_MENU_SAIR:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;

        case ID_MENU_SOBRE:
            MessageBox(hWnd,
                TEXT("Nome: Tiago Cabral\nNúmero: 2018020685"),
                TEXT("Sobre o Grupo"),
                MB_OK | MB_ICONINFORMATION);
            break;
        }
        break;

    case WM_CLOSE:
        KillTimer(hWnd, 1);
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

BOOL InitSharedMemory(ControlData* cdata) {
    BOOL firstProcess = FALSE;
    cdata->hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);

    if (cdata->hMapFile == NULL) {
        cdata->hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,
            NULL,
            PAGE_READWRITE, 0, sizeof(SharedMem), SHM_NAME);

        firstProcess = TRUE;
        if (cdata->hMapFile == NULL) {
            _tprintf(TEXT("ERROR: CreateFileMapping", GetLastError()));
            return FALSE;
        }
    }

    cdata->shm = (SharedMem*)MapViewOfFile(cdata->hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMem));
    if (cdata->shm == NULL) {
        _tprintf(TEXT("Error: MapViewOfFile (%d)\n"), GetLastError());
        CloseHandle(cdata->hMapFile);
        return FALSE;
    }

    if (firstProcess) {
        cdata->shm->writePos = 0;
        cdata->shm->readPos = 0;
        cdata->shm->numLetras = 0;

        for (int i = 0; i < MAXCLIENTES; i++) {
            memset(cdata->shm->clients[i].name, 0, sizeof(cdata->shm->clients[i].name));
            cdata->shm->clients[i].points = 0;
        }
    }

    cdata->hMutex = CreateMutex(NULL, FALSE, MUTEX_SERVER_NAME);

    if (cdata->hMutex == NULL) {
        _tprintf(TEXT("Error: CreateMutex (%d)\n"), GetLastError());
        UnmapViewOfFile(cdata->shm);
        CloseHandle(cdata->hMapFile);
        return FALSE;
    }
    return TRUE;
}

void CleanupSharedMemory(ControlData* cdata) {
    if (cdata->shm) {
        UnmapViewOfFile(cdata->shm);
    }
    if (cdata->hMapFile) {
        CloseHandle(cdata->hMapFile);
    }
    if (cdata->hMutex) {
        CloseHandle(cdata->hMutex);
    }
}