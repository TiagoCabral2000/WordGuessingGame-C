# TP_SO2_2425

**Multiplayer word guessing game with shared memory and named pipes for Windows – Operating Systems II project**  
A project developed within the scope of the Operating Systems II course  
Final grade: 96%
Developed in **C** using **Unicode** on the **Windows** platform

---

## 🕹️ Game Overview

This is a multiplayer word-guessing game where players identify valid words using visible letters shown in real time. The system supports up to **20 players**, including human users and automated bots.

All interprocess communication is done through **named pipes** and **shared memory**, with appropriate **synchronization** mechanisms. The game logic is handled entirely on the local machine (no network communication involved).

---

## 🧩 Features

### 🧠 Central Game Manager (`Árbitro`)
- Controls game logic and rules
- Periodically generates new random letters
- Validates guessed words using a dictionary
- Handles player registration and scoring
- Accepts admin commands:
  - `listar` – List players and scores
  - `excluir <username>` – Kick player
  - `iniciarbot <username>` – Start bot player
  - `acelerar` / `travar` – Adjust letter cadence
  - `encerrar` – Terminate the game

### 👤 Player Interface (`jogoui`)
- Console-based interface
- Launched with: `./jogoui <username>`
- Supports the following commands:
  - Typing a word → tries to guess it
  - `:pont` – Show current score
  - `:jogs` – Show list of players
  - `:sair` – Exit the game
- Unique usernames enforced

### 🤖 Bot Players (`bot`)
- Simulate player behavior with automated guessing
- Configurable/random reaction times
- Launched by the game manager
- Print attempted words and scores to console

### 🖥️ Optional Visual Panel (`painel`)
- Real-time **Win32 GUI**
- Displays:
  - Latest correctly guessed word
  - Visible letters
  - Sorted leaderboard with scores
- Does not require user input or interaction

---

## 🔧 Technical Details

- **Language:** C (Unicode support via wide-character functions)
- **Platform:** Windows only
- **IPC Mechanisms:**
  - Named pipes (for communication)
  - Shared memory (for letter board)
  - Mutexes/events for synchronization
- **Registry-based Configuration:**
  - `HKEY_CURRENT_USER\Software\TrabSO2`
    - `RITMO`: Letter update interval (seconds)
    - `MAXLETRAS`: Maximum number of visible letters
  - Defaults: `RITMO = 3`, `MAXLETRAS = 6`
- **No persistent data:** Game state resets on exit

---

## 📝 How to Run

1. Build all components with a C compiler that supports Unicode on Windows (e.g., MSVC).
2. Run the referee first:
   ```bash
   ./arbitro
