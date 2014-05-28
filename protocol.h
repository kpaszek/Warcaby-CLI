#ifndef protocol_h
#define protocol_h

// pomocnicze  struktury, komunikaty i deklaracje (poza protokolem)

#define MAX_PLAYERS 32
#define MAX_GAMES (MAX_PLAYERS/2)

typedef struct player_info
{
      char nickname[32];
      int color,level;
      int qid,shid,gid;

}player;

typedef struct game_info
{
	int board[8][8];
	int color;
	int spectid[MAX_PLAYERS];
}room;

// typ 100 k-k  s-s
typedef struct terminate
{
	long type;
}nonprotocol_msg_term;

// struktury z protokolu

typedef struct preferences
{
	int level,color;
} prefs;

struct game
{
        char player1[32],player2[32];
        int game_id,queue_id;
};

/* Etap 1 */

//typ 1 k-s
typedef struct new_client
{
        long type;
        char nickname[32];
        int queue_id,shm_preferences;

}msg_log;

//typ1 s-k
typedef struct login_response
{
        long type;
        int status;
}msg_log_resp;

/* Etap 2 */


//typ 3 s-k
typedef struct game_list
{
	long type;
	struct game games[32];
}msg_gms_lst;

//typ 2 k-s
typedef struct player_request
{
	long type;
	int command;
}msg_cmd;

//typ 6 k - g
typedef struct join_game
{
	long type;
	char nickname[32];
	int queue_id;
}msg_join;

//typ 10
typedef struct new_game
{
	long type;
	int queue_id;
}msg_ngm;

//typ 7 s - k
typedef struct join_spectator
{
	long type;
	int board[8][8];
}msg_spect_resp;

/* Etap 3 */

//typ 5 s-k
typedef struct start_game
{
	long type;
	int first;
}msg_start;

//typ 8 k-s
typedef struct spectator_left
{
	long type;
	char nickname[32];
	int queue_id;
}msg_spect_left;

//typ 9  s-k k-g
typedef struct  end_game
{
        long type;
        int win;
}msg_end;

//typ 4 (k-s) i 6 (s-k)
typedef struct
{
	long type;
	int from_x,from_y,to_x,to_y,turn;
	int pawn_removed_count;
	int pawn_removed[16][2];
}msg_send_mv;

#endif
