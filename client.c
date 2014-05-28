#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include "protocol.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>

int mainQueue = -1;
int gameQueue = -1;
int privateQueue = -1;
int shmPrefsId = -1;
int pid;
prefs* playerPrefs;
int board[8][8];
char nick[32];
int playerColor;
int enemyColor;
int shown;
int i, j;
int turn;

	
msg_ngm newGame;
msg_start startMsg;
msg_join joinMsg;

//procedura obslugujaca SIGINT
void endGame () {
	printf("Wylogowywanie się...\n");
	msg_cmd logout = {2, 2};
	msgsnd(privateQueue, &logout, sizeof(logout) - sizeof(logout.type), 0);
	//odczekanie na to, by serwer odebrał komunikat
	sleep(2);
	printf("Usuwanie stworzonych struktur...\n");
	if (shmPrefsId != -1) if (shmctl(shmPrefsId, IPC_RMID, 0) == -1) perror("rm shm");
	if (privateQueue != -1) if (msgctl(privateQueue, IPC_RMID, 0) == -1) perror("rm privateQueue");
	if (gameQueue != -1) if (msgctl(gameQueue, IPC_RMID, 0) == -1) perror("rm gameQueue");
	printf("Klient się kończy.\n");
	exit(1);
}

void prepareBoard() {
	int x,y;
	for (y=0; y<8; y++) {
		for (x=0; x<8; x++) {
			if(y<3 && (x+y)%2==0) board[y][x]=1;
			else if(y>4 && y<8 && (x+y)%2==0) board[y][x]=-1;
			else board[y][x]=0;
		}
	}
}

void showBoard() {
	int x,y;
	for (y=0; y<8; y++) {
		printf ("%d ", y);
		for (x=0; x<8; x++) {
			if (board[y][x] == -1) printf("C ");
			else if (board[y][x] == 1) printf("B ");
			else if (board[y][x] == 0 && (x+y)%2==0) printf("+ ");
			else printf("  ");
		}
		printf("\n");
	}
	printf("X 0 1 2 3 4 5 6 7\n");
}

void prepareStructs() {
	if ((mainQueue = msgget(42, 0777)) == -1) {
		perror("msgget mainQueue");
		exit(1);
	}
	
	//utworzenie swojej prywatnej kolejki
	//int privateQueue;
	
	pid = getpid();
	if ((privateQueue = msgget(pid, IPC_CREAT | 0777)) == -1) {
		perror("msgget privateQueue");
		exit(1);
	}

	//utworzenie wspoldzielonej pamieci player playerInfo;
	if ((shmPrefsId = shmget(pid, sizeof(prefs), IPC_CREAT | 0777)) == -1) {
		perror("shared memory");
		exit(1);
	}
	playerPrefs = (prefs*)shmat(shmPrefsId, NULL, 0);
	playerPrefs->color = -1;
	playerPrefs->level = -1;
}

void loginProcedure() {
	//tworzenie komunikatu do wysłania przy logowaniu
	msg_log_resp loginResponse;
	msg_log login = {1, "hereBeLogin", pid, pid};
	int logged = 0;
	
	//Podanie preferencji i zapisanie ich w pamięci współdzielonej
	while (!logged) {
		printf("Podaj swój nick: ");
		scanf("%s", nick);
		strcpy(login.nickname, nick);
		while(playerPrefs->color > 1 || playerPrefs->color < 0) {
			printf("Podaj kolor pionków (0 - czarne, 1 - biale): ");
			scanf("%d", &playerPrefs->color);
		}
		while(playerPrefs->level < 0 || playerPrefs->level > 3) {
			printf("Podaj swój poziom (0-3): ");
			scanf("%d", &playerPrefs->level);
		}

		if (playerPrefs->color == 1) {playerColor = 1; enemyColor = -1;}
		else {playerColor = -1; enemyColor = 1;}
	
		//wysłanie komunikatu logowania do serwera
		msgsnd(mainQueue, &login, sizeof(login) - sizeof(login.type), 0);
		printf("Wysłałem komunikat. Czekam na odpowiedź...\n");
		msgrcv(privateQueue, &loginResponse, sizeof(loginResponse) - sizeof(login.type), 0, 0);
		if (loginResponse.status == 0) {printf("Zalogowano!\n"); logged = 1;}
		else {if (loginResponse.status == 1) printf("Nick zajęty. Wybierz inny\n"); else printf("Serwer pełny.\n");}
	}
}

int chooseCommand() {
	int wybor;
	printf("0 - Listowanie gier\n1 - Nowa gra\n2 - Wyloguj\n");
	while (wybor < 0 || wybor > 2) scanf("%d", &wybor);
	msg_cmd command = {2, wybor};
	msgsnd(privateQueue, &command, sizeof(command) - sizeof(command.type), 0);
	return wybor;
}

void listGames() {
	msg_gms_lst gamesList;
	msgrcv(privateQueue, &gamesList, sizeof(gamesList) - sizeof(gamesList.type), 3, 0);
	int i=0;
	int gra = 0;
	for (i=0; i<MAX_GAMES; i++) {
		if (gamesList.games[i].game_id!=-1) {
			printf("Gra nr %d:\n", gamesList.games[i].game_id);
			printf("Gracz 1: %s\n", gamesList.games[i].player1);
			printf("Gracz 2: %s\n", gamesList.games[i].player2);
			printf("game.queue_id: %d\n", gamesList.games[i].queue_id);
		}
	}
	printf("Wpisz ID gry, z którą chcesz się połączyć:");
	scanf("%d", &gra);
	joinMsg.type = 6;
	strcpy(joinMsg.nickname, nick);
	joinMsg.queue_id = privateQueue;
	printf("gameQueue (%d)  = gamesList.games[gra].queue_id (%d)\n", gameQueue, gamesList.games[gra].queue_id);
	gameQueue = gamesList.games[gra].queue_id;
	msgsnd(gameQueue, &joinMsg, sizeof(joinMsg) - sizeof(joinMsg.type), 0);
	printf("Wyslano komunikat zaczecia na kolejkę %d...\n", gameQueue);
	printf("Oczekiwanie na komunikat startu...\n");
	msgrcv(privateQueue, &startMsg, sizeof(startMsg) - sizeof(startMsg.type), 5, 0);
	if (startMsg.first == 1) turn = 1;
	else turn = 0;
	printf("Gra się rozpoczyna.\n");
}

void createNewGame() {
	msgrcv(privateQueue, &newGame, sizeof(newGame) - sizeof(newGame.type), 10, 0);
	gameQueue = msgget(newGame.queue_id, 0777);
	printf("Nowa gra założona. Oczekiwanie na dołączenie przeciwnika.\n");
	msgrcv(privateQueue, &startMsg, sizeof(startMsg) - sizeof(startMsg.type), 5, 0);
	if (startMsg.first == 1) turn = 1;
	else turn = 0;
	printf("Gra się rozpoczyna\n");
}

void game() {
	msg_end endMsg;
	endMsg.type = 9;
	endMsg.win = enemyColor;
	msg_send_mv move;
	while((msgrcv(privateQueue, &endMsg, sizeof(endMsg) - sizeof(long), 9, IPC_NOWAIT)) < 0) {
		int i;
		if (turn) {
			printf("Twoj ruch!\n");
			move.type = 4;
			showBoard();
			printf("Ktory pionek przesuwasz: ");
			scanf("%d %d", &move.from_x, &move.from_y);
			if (move.from_x == -1) { 
				printf("Wysyłam na kolejkę %d endMsg...\n", gameQueue);
				msgsnd(gameQueue, &endMsg, sizeof(endMsg) - sizeof(endMsg.type), 0);
			} else {
			board[move.from_y][move.from_x] = 0;
			printf("Dokad go przesuwasz: ");
			scanf("%d %d", &move.to_x, &move.to_y);
			board[move.to_y][move.to_x] = playerColor;
			printf("Ile pionkow zdjales? ");
			scanf("%d", &move.pawn_removed_count);
			for (i=0; i<move.pawn_removed_count; i++) {
				printf("Podaj wspolrzedne %d-ego zdjetego pionka", i);
				scanf("%d %d", &move.pawn_removed[i][0], &move.pawn_removed[i][1]);
				board[move.pawn_removed[i][1]][move.pawn_removed[i][0]] = 0;
			}
			msgsnd(privateQueue, &move, sizeof(move) - sizeof(move.type), 0);
			showBoard(); } 
			turn = 0;
		} else if ((msgrcv(privateQueue, &move, sizeof(move) - sizeof(move.type), 6, IPC_NOWAIT)) >= 0) {
			printf("Ruch przeciwnika!\n");
			board[move.from_y][move.from_x] = 0;
			board[move.to_y][move.to_x] = enemyColor;
			for (i=0; i<move.pawn_removed_count; i++) {
				board[move.pawn_removed[i][1]][move.pawn_removed[i][0]] = 0;
			}
			//showBoard();
			turn = 1;
		}
		sleep(1);
	}

	if(endMsg.win == 1) printf("Wygraly biale.\n");
	else printf("Wygraly czarne.\n");
}

int main (int argc, char* argv[]) {
	signal(SIGINT, endGame);
	prepareStructs();
	
	while (1) {
		prepareBoard();
		loginProcedure();
		//wybór polecenia
		int wybor = chooseCommand();
		printf("Oczekiwanie na kolejce %d.\n", privateQueue);
		if (wybor == 0) listGames();
		else if (wybor == 1) createNewGame();
		else if (wybor == 2) endGame();
		game();
		sleep(1);
	}
	//endGame();
	return 0;
}
