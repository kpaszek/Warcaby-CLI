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

player players[MAX_PLAYERS];
struct game games[MAX_GAMES];
room rooms[MAX_GAMES];
int currentPlayers = 0;
int currentGames = 0;
int mainQueue;
int i;
int privateQueue[MAX_PLAYERS];

msg_log login;

void printPlayers() {
	int i=0;
	for (i=0; i<currentPlayers; i++) {
		printf("Player %d: %s (%d %d)\n", i, players[i].nickname, players[i].color, players[i].level);
	}
}

void printGames() {
	int i=0;
	for (i=0; i<currentGames; i++) {
		printf("Game %d: %s vs %s (%d %d)\n", i, games[i].player1, games[i].player2, games[i].game_id, games[i].queue_id);
	}
}

void prepareStructs() {
	if ((mainQueue = msgget(42, IPC_CREAT | 0777)) == -1) {
		perror("msgget mainQueue");
		exit(1);
	}

	//utworzenie i zainicjalizowanie potrzebnych struktur
	
	for (i=0; i<MAX_GAMES; i++) {
		games[i].game_id = -1;
		int j;
		for (j=0; j<MAX_PLAYERS; j++) {
			rooms[i].spectid[j] = -1;
		}
	}
	
	for (i=0 ; i<MAX_PLAYERS; i++) {
		players[i].qid = -1;
		players[i].gid = -1;
		privateQueue[i] = -1;
	}
}

void listenForNewPlayers() {
	if ((msgrcv(mainQueue, &login, sizeof(login)-sizeof(login.type), 1, IPC_NOWAIT)) >= 0 ) {
		int tempPrivateQueue = msgget(login.queue_id, 0666);
		printf("Odebrałem komunikat logowania.\n");
		if (currentPlayers < MAX_PLAYERS) {
		int nickError = 0;
		for (i = 0; i<MAX_PLAYERS; i++) {
			if ((strcmp(players[i].nickname, login.nickname)) == 0) nickError = 1;
		}
		if (nickError) {
			msg_log_resp loginResponse = {1,1};
			msgsnd(tempPrivateQueue, &loginResponse, sizeof(loginResponse) - sizeof(loginResponse.type), 0);
		} else {
			printf("Otrzymywanie klucza pamięci współdzielonej...\n");
			int shmPrefsId = shmget(login.shm_preferences, sizeof(prefs), 0777);
			printf("Przyłączanie pamięci współdzielonej...\n");
			prefs* playerPrefs = (prefs*)shmat(shmPrefsId, NULL, 0);
			printf("Kopiowanie danych o graczu do lokalnych struktur...\n");
			//szukanie pierwszego wolnego miejsca
			int place = 0;
			for (i = 0; i<MAX_PLAYERS; i++) {
				if (players[i].qid == -1) {place = i; break;}
			}
			printf("Znaleziono miejsce: %d\n", place);
			strcpy(players[place].nickname, login.nickname);
			players[place].color = playerPrefs->color;
			players[place].level = playerPrefs->level;
			players[place].qid = tempPrivateQueue;
			players[place].shid = login.shm_preferences;
			currentPlayers++;
			printPlayers();
			//podczepienie się pod prywatną kolejkę klienta
			privateQueue[place] = tempPrivateQueue;
			//odesłanie potwierdzenia zalogowania
			msg_log_resp loginResponse = {1, 0};
			msgsnd(privateQueue[place], &loginResponse, sizeof(loginResponse) - sizeof(loginResponse.type), 0);
			}
		} else {
			printf("Serwer pełen\n");
			msg_log_resp loginResponse = {1, 2};
			msgsnd(tempPrivateQueue, &loginResponse, sizeof(loginResponse) - sizeof(long), 0);
		}
	}
	return;
}

void listGames(int p) {
	msg_gms_lst gamesList;
	for (i=0; i<MAX_GAMES; i++) {
		gamesList.games[i].game_id = -1;
	}
	printf("Otrzymano polecenie LISTGAMES. Wysyłanie listy...\n");
	int j;
	gamesList.type = 3;
	for (j=0; j<MAX_PLAYERS; j++) {
		if (players[j].qid != -1) {
			if (players[j].level == players[p].level && players[j].color != players[p].color) {
				int found = players[j].gid;
				int gid = games[found].game_id;
				strcpy(gamesList.games[gid].player1, games[found].player1);
				strcpy(gamesList.games[gid].player2, games[found].player2);
				gamesList.games[gid].game_id = games[found].game_id;
				gamesList.games[gid].queue_id = games[found].queue_id;
				printf("game.game_id: %d\n", games[found].game_id);
				printf("game.queue_id: %d\n", games[found].queue_id);
			} 
		}
	}
	msgsnd(privateQueue[p], &gamesList, sizeof(gamesList) - sizeof(long), 0);
	printf("Wysłano listę z grami.\n");
}

void createNewGame(int p) {
	msg_ngm newGame = {10, 0};
	printf("Zakładanie nowej gry dla gracza %d...\n", p);
	key_t key = ftok("serv.c", currentGames+1);
	int tempGameQueue = msgget(key, IPC_CREAT | 0666);
	players[p].gid = tempGameQueue;
	int place = 0;
	int j;
	for (j = 0; j<MAX_GAMES; j++) {
		if (games[j].game_id == -1) {place = j; break;}
	}
	printf("Kopiowanie imienia gracza %d do gry nr %d...\n", p, place);
	strcpy(games[place].player1, players[p].nickname);
	strcpy(games[place].player2, "Oczekiwanie...");
	games[place].game_id = place;
	players[p].gid = place;
	games[place].queue_id = tempGameQueue;
	newGame.queue_id = tempGameQueue;
	rooms[place].color = players[p].color;
	rooms[place].spectid[0] = players[p].qid;
	msgsnd(privateQueue[p], &newGame, sizeof(newGame) - sizeof(long), 0);
	currentGames++;
	printGames();						
	printf("Nowa gra utworzona.\n");
}

void addSecondPlayer(int p, msg_join joinMsg) {
	msg_start startMsg;
	//trzeba jeszcze sprawdzić czy jest to drugi czy trzeci, czwarty gracz 
	printf("Zgłoszenie się drugiego gracza do gry %d.\n", p);
	strcpy(games[p].player2, joinMsg.nickname);
	//szukanie drugiego gracza w lokalnych strukturach by dopisać mu
	//potrzebne dane, wtf
	printf("Przypisanie drugiemu graczowi id gry...\n");
	int j=0;
	for (j = 0; j<MAX_PLAYERS; j++) {
		if (!strcmp(joinMsg.nickname, players[j].nickname)) {
			players[j].gid = games[p].game_id;
			rooms[p].spectid[1] = players[j].qid;
		}
	}
	//wysłanie graczom komunikatu o rozpoczęciu gry
	startMsg.type = 5;
	if (rooms[p].color == 0) startMsg.first = 0;
	else startMsg.first = 1;
	printf("Wysłanie start do pierwszego gracza (%d)...\n", rooms[p].spectid[0]);
	msgsnd(rooms[p].spectid[0], &startMsg, sizeof(startMsg) - sizeof(long), 0);
	if (startMsg.first == 0) startMsg.first = 1;
	else startMsg.first = 0;
	printf("Wysłanie start do drugiego gracza (%d)...\n", rooms[p].spectid[1]);
	msgsnd(rooms[p].spectid[1], &startMsg, sizeof(startMsg) - sizeof(long), 0);
	printf("Kolejka gry to %d.\n", games[i].queue_id);
}

void endGame(int p) {
	msg_end endMsg2;
	endMsg2.type = 9;
	printf("Odebrałem informację o końcu gry...\n");
	printf("Wysyłam informację o końcu gry do pierwszego gracza... (%d)\n", rooms[p].spectid[0]);					
	msgsnd(rooms[p].spectid[0], &endMsg2, sizeof(endMsg2) - sizeof(long), 0);
	printf("Wysyłam informację o końcu gry do drugiego gracza... (%d)\n", rooms[p].spectid[1]);
	msgsnd(rooms[p].spectid[1], &endMsg2, sizeof(endMsg2) - sizeof(long), 0);
	int who1=-1;
	int who2, j;
	for (j=0; j<MAX_PLAYERS; j++) {
		if (players[j].gid == games[p].game_id) {
			if (who1 != -1) who1=j; else who2=j;
		}
	}
	games[p].game_id = -1;
	players[who1].gid = -1;
	players[who2].gid = -1;
	rooms[p].color = -1;
	int i;
	for (i = 0; i < MAX_PLAYERS; i++) {
		rooms[p].spectid[i] = -1;
	}
	printGames();
}

void logout(int p) {
	printf("Gracz %d się wylogował.\n", p);
	int place = players[p].gid;
	players[p].qid = -1;
	players[p].gid = -1;
	privateQueue[p] = -1;
	//trzeba jeszcze usuwać takiego gracza z games
	//jeśli był w grze to wyłączyć z gry
	if (games[place].game_id != -1) {
		//jeśli gra w ogóle zaczęta
		//to w razie jeśli gra była w trakcie
		if (rooms[place].spectid[1] != -1) {
			endGame(p);	
		}
		games[place].game_id = -1;
		games[place].queue_id = -1;
		currentGames--;
	}
	currentPlayers--;
}

void endServer() {
	//int i;
	//for (i = 0; i<MAX_GAMES; i++) {
	//	if (games[i].queue_id == -1) if (msgctl(games[i].queue_id, IPC_RMID, 0) == -1) perror("rm GameQueue");
	//}
	if (mainQueue != -1) if (msgctl(mainQueue, IPC_RMID, 0) == -1) perror("rm mainQueue");
	printf("Serwer kończy działanie.\n");
	exit(1);
}

int main (int argc, char* argv[]) {
	printf("Server start\n");
	//utworzenie glownej kolejki o id 42 powiazanej z plikiem serv.c
	prepareStructs();
	msg_cmd command;
	
	//główna pętla programu
	while (1) {
		//ustawienie obsługi sygnału wywołanego C-c
		//signal(SIGINT, endGame);
		
		listenForNewPlayers();
		
		//nasłuchiwanie poleceń od wszystkich zalogowanych graczy
		for (i=0; i<currentPlayers; i++) {
			//jeśli i-ty gracz wyśle polecenie
			if ((msgrcv(privateQueue[i], &command, sizeof(command) - sizeof(long), 2, IPC_NOWAIT)) >= 0) {
				printf("Przychodzi polecenie od gracza %d...\n", i);
				if (command.command == 0) {
					listGames(i);
				} else if (command.command == 1) {
					createNewGame(i);
				} else if (command.command == 2) {
					logout(i);
				}
			}
		}
		
		msg_join joinMsg;
		msg_end endMsg;
		

		//nasłuchiwanie na jednoosobowych pokojach na dołączenie
		for (i=0; i<MAX_GAMES; i++) {
			if (games[i].game_id != -1) {
				if ((msgrcv(games[i].queue_id, &joinMsg, sizeof(joinMsg) - sizeof(long), 6, IPC_NOWAIT)) >= 0) {
					addSecondPlayer(i, joinMsg);
				} else if ((msgrcv(games[i].queue_id, &endMsg, sizeof(endMsg) - sizeof(long), 9, IPC_NOWAIT)) >= 0) {
					endGame(i);
				}
			}
		}

		//w tym momencie zaczyna się nasłuchiwanie na kolejce gracza na ruch, jeśli taki się zdarzy
		msg_send_mv moveMsg;
		for (i=0; i<MAX_PLAYERS; i++) {
			if (players[i].gid!=-1) {
				if ((msgrcv(players[i].qid, &moveMsg, sizeof(moveMsg) - sizeof(long), 4, IPC_NOWAIT)) >= 0) {
					printf("Odebrano wiadomość o ruchu...\n");
					int toWhom;
					if (players[i].qid == rooms[players[i].gid].spectid[0]) toWhom = rooms[players[i].gid].spectid[1];
					else toWhom = rooms[players[i].gid].spectid[0];
					moveMsg.type = 6;
					printf("Przekazywanie wiadomości do drugiego gracza...\n");
					//to trzeba będzie zmienić, by wysyłało do wszystkich ze spectid innych od -1
					//czyli do wszystkich obserwatorów
					msgsnd(toWhom, &moveMsg, sizeof(moveMsg) - sizeof(long), 0);
					}
				 
			}
		}
		//to odsyłany jest do obserwatorów i drugiego gracza
		sleep(1);
	}
	
	return 0;
}
