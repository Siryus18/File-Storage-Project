#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/un.h> 	
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <ftw.h>

#include <api.h>
#include <partialIO.h>

#define UNIX_PATH_MAX 108 
//#define SOCKNAME "./mysock"
#define CMDSIZE 256
#define BUFSIZE 10000 	// 10KB

typedef struct struct_cmd {		
	char cmd;					// nome del comando
	char *arg;					// (eventuale) argomento del comando
	struct struct_cmd *next;	// puntatore al prossimo comando nella lista
} cmdT;

typedef struct struct_cmd_w {
	char Directory[256];
	int print;
	int maxN;
	int currN;
}	cmd_w_T;

// variabile globale di appoggio per il comando -w
static cmd_w_T *wT;

int addCmd(cmdT **cmdList, char cmd, char *arg);
int execute(cmdT *cmdList, int print);
int destroyCmdList(cmdT *cmdList);
int cmd_f(char* socket);
int cmd_w(char *dirname, char *Directory, int print);
int cmd_w_aux(const char *ftw_filePath, const struct stat *ptr, int flag);
int cmd_W(const char *filelist, char *Directory, int print);
int cmd_r(const char *filelist, char *directory, int print);

void testOpenFile() {
	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 550;

	if (openConnection("mysock", 100, ts) != 0) {
		return;
	}
	printf("INIZIO TEST OPENFILE\n");
	printf("Usare maxFiles = 3, un file gia' dentro il server\n");
	printf("Creo un file non lockato. Dovrebbe dare OK.\n");
	if (openFile("fileNONLOCKED", 1) == -1) {
		perror("openFile");
	}

	printf("Creo un file lockato. Dovrebbe dare OK.\n");
	if (openFile("fileLOCKED", 3) == -1) {
		perror("openFile");
	}

	printf("Creo un file gia' esistente. Dovrebbe dare ER.\n");
	if (openFile("test/file2.txt", 1) == -1) {
		perror("openFile");
	}

	printf("Apro un file che esiste ma e' lockato da un client diverso. Dovrebbe dare ER.\n");
	if (openFile("test/file2.txt", 2) == -1) {
		perror("openFile");
	}

	printf("Creo un file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED2", 3) == -1) {
		perror("openFile");
	}

	printf("Apro un file che esiste e non e' lockato. Dovrebbe dare OK.\n");
	if (openFile("fileNONLOCKED", 2) == -1) {
		perror("openFile");
	}

	printf("Creo un altro file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED3", 3) == -1) {
		perror("openFile");
	}

	printf("Creo l'ultimo file ma la coda e' piena. Dovrebbe dare ES.\n");
	if (openFile("fileLOCKED4", 3) == -1) {
		perror("openFile");
	}

	printf("FINE TEST OPENFILE\n");
}

void testWriteFile() {
	printf("INIZIO TEST WRITEFILE\n");
	printf("Usare maxFiles = 2, maxSize = 5, due file gia' dentro il server\n");
	printf("Scrivo un file che non ho aperto. Mi aspetto: perror\n");
	if (writeFile("test/filepesante", NULL) == -1) {
		perror("writeFile");
	}

	printf("Creo un file non lockato. Mi aspetto: ES\n");
	if (openFile("test/fileleggero", 1) == -1) {
		perror("openFile");
	}

	printf("Scrivo un file creato e aperto da me. Mi aspetto: OK\n");
	if (writeFile("test/fileleggero", NULL) == -1) {
		perror("writeFile");
	}

	printf("Creo un altro file prima di chiudere l'altro. Mi aspetto: perror\n");
	if (openFile("test/filepesante", 3) == -1) {
		perror("openFile");
	}
	printf("FINE TEST WRITEFILE\n");
}

void testAppendToFile() {
	printf("INIZIO TEST APPENDTOFILE\n");
	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 550;

	if (openConnection("mysock", 100, ts) != 0) {
		return;
	}

	void *buf = malloc(256);
	char str[256] = "Contenuto da appendere al file.\n";
	size_t size = strlen(str)+1;
	memcpy(buf, str, size);

	printf("Scrivo un file che non ho aperto. Mi aspetto: perror\n");
	if (appendToFile("test/filepesante", buf, size, "Wtest") == -1) {
		perror("writeFile");
	}

	printf("Creo un file non lockato. Mi aspetto: ES\n");
	if (openFile("test/fileleggero", 1) == -1) {
		perror("openFile");
	}

	printf("Faccio l'append su un file creato e aperto da me. Mi aspetto: OK\n");
	if (appendToFile("test/fileleggero", buf, size, "Wtest") == -1) {
		perror("writeFile");
	}

	printf("Creo un altro file prima di chiudere l'altro. Mi aspetto: perror\n");
	if (openFile("test/filepesante", 3) == -1) {
		perror("openFile");
	}

	printf("FINE TEST APPENDTOFILE\n");

	free(buf);
}

void stressTest(int startNum) {
	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 550;

	errno = 0;
	if (openConnection("mysock", 100, ts) != 0) {
		perror("openConnection");
		return;
	}

	char *str = malloc(10);
	for (int i = startNum; i < startNum+5; i++) {
		sprintf(str, "%d", i);
		printf("Creo un file.\n");
		if (openFile(str, 1) == -1) {
			perror("openFile");
		}
		
		sleep(1);

		if (closeFile(str) == -1) {
			perror("closeFile");
		}
	}

	free(str);
}

int main(int argc, char* argv[]) {
	struct sigaction siga;

	printf("File Storage Client avviato.\n");
	fflush(stdout);

	if (argc < 2) {
		printf("Usage: ecc... Nessun argomento! TO-DO\n");
		return -1;
	}

	// ignoro il segnale SIGPIPE
	memset(&siga, 0, sizeof(siga));
	siga.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &siga, NULL) == -1) {
		perror("sigaction.\n");
		return 1;
	} 

	int opt;
	int f = 0, h = 0, p = 0;	// variabili per tenere traccia dei comandi che possono essere utilizzati solo una volta
	char args[256];

	// creo la lista di comandi
	cmdT *cmdList = NULL;
	cmdList = calloc(1, sizeof(cmdT));
	
	// ciclo per il parsing dei comandi
	while ((opt = getopt(argc, argv, "hpf:t:w:W:D:r:d:")) != -1) {
		switch (opt) {
			// stampa la lista di tutte le opzioni accettate dal client e termina immediatamente
			case 'h':
				if (h) {
					printf("Il comando -h puo' essere usato solo una volta.\n");
					destroyCmdList(cmdList);
					return 1;
				}

				else {
					addCmd(&cmdList, opt, "");
					h = 1;
				}
				break;
			
			// specifica il nome del socket AF_UNIX a cui connettersi
			case 'f':
				if (f) {
					printf("Il comando -f puo' essere usato solo una volta.\n");
					destroyCmdList(cmdList);
					return 1;
				}

				else {
					memset(args, '\0', 256);
					strncpy(args, optarg, strlen(optarg)+1);
					addCmd(&cmdList, opt, optarg);
					f = 1;
				}
				break;

			// abilita le stampe su stdout per ogni operazione
			case 'p':
				if (p) {
					printf("Il comando -p puo' essere usato solo una volta.\n");
					destroyCmdList(cmdList);
					return 1;
				}

				else {
					printInfo(1);
					p = 1;
				}
				break;

			case 'w':	// scrivi sul server 'n' file contenuti in una cartella
				// alloca la memoria per la variabile globale che contiene le informazioni necessarie per cmd_w
				if ((wT = (cmd_w_T*) calloc(1, sizeof(cmd_w_T))) == NULL) {
					perror("calloc wT");
					destroyCmdList(cmdList);
					return 1;
				}

				memset(args, '\0', 256);
				strncpy(args, optarg, strlen(optarg)+1);
				addCmd(&cmdList, opt, args);
				break;

			case 't':	// tempo in millisecondi che intercorre tra l’invio di due richieste successive al server
			case 'W':	// lista di nomi di file da scrivere nel server, separati da virgole
			case 'r': 	// lista di nomi di file da leggere dal server, separati da virgole
 			case 'D':	// cartella dove vengono scritti i file che il server rimuove a seguito di capacity misses in scrittura
 			case 'd':	// cartella dove vengono scritti i file letti dal server
				memset(args, '\0', 256);
				strncpy(args, optarg, strlen(optarg)+1);
				addCmd(&cmdList, opt, args);
				break;

			// argomento non riconosciuto
			case '?': default:
				break;
		}
	}

	/*
	int num = strtol(argv[1], NULL, 0);
	stressTest(num);
	*/

	if (execute(cmdList, p) == -1) {
		perror("execute");
		if (wT) {
			free(wT);
		}
		return 1;
	}

	//testAppendToFile();
	//testOpenFile();
	//testWriteFile();

	if (destroyCmdList(cmdList) == -1) {
		perror("destroyCmdList");
		if (wT) {
			free(wT);
		}
		return 1;
	}
	
	if (wT) {
		free(wT);
	}
	return 0;
}

// aggiunge un comando in fondo alla lista
int addCmd(cmdT **cmdList, char cmd, char *arg) {
	// controllo la validita' degli argomenti 
	if (!cmdList || !arg) {
		errno = EINVAL;
		return -1;
	}

	// creo il nuovo elemento allocandone la memoria
	cmdT *new = NULL;
	if ((new = malloc(sizeof(cmdT))) == NULL) {
		perror("malloc cmdT");
		return -1;
	}

	new->cmd = cmd;

	if ((new->arg = malloc(CMDSIZE)) == NULL) {
		perror("malloc arg");
		free(new);
		return -1;
	}

	strncpy(new->arg, arg, strlen(arg)+1);

	new->next = NULL;

	cmdT *tail = *cmdList;

	// se la lista era vuota, il comando aggiunto diventa il primo della lista
	if (*cmdList == NULL) {
		*cmdList = tail;
	}

	// altrimenti, scorro tutta la lista e aggiungo il comando come ultimo elemento
	else {
		while (tail->next) {
			tail = tail->next;
		}

		tail->next = new;
	}

	return 0;
}

// esegue tutti i comandi nella lista, uno alla volta
int execute(cmdT *cmdList, int print) {
	// controllo la validita' dell'argomento
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	int ok = 1;			// esito del comando
	cmdT *temp = NULL;
	temp = cmdList;
	char sock[256] = "";	// socket che viene impostato con il comando -f
	int w = 0;				// se = 1, l'ultimo comando letto e' una scrittura (-w o -W)
	int r = 0;				// se = 1, l'ultimo comando letto e' una lettura (-r o -R)
	char Dir[256] = "";		// cartella in cui scrivere i file espulsi dal server a seguito di capacity misses in scrittura
	char dir[256] = "";		// cartella in cui scrivere i file letti dal server

	// variabili per gestire il comando -t
	struct timespec tim1, tim2;
	tim1.tv_sec = 0;
	tim1.tv_nsec = 0;
	int sec = 0;
	double msec = 0;
	long num = 0;

	while(temp) {
		switch (temp->cmd) {
			// stampa il messaggio di aiuto
			case 'h':
				w = 0;
				printf("\nMessaggio di aiuto... TO-DO");
				break;

			// connettiti al socket AF_UNIX specificato
			case 'f':
				w = 0;
				r = 0;
				strncpy(sock, temp->arg, strlen(temp->arg)+1);

				if (cmd_f(temp->arg) != 0) {
					if (print) {
						perror("-f");
					}
					ok = 0;
				}

				else {
					ok = 1;
				}

				// controllo se devo stampare su stdout
				if (print) {
					printf("\nf - Connessione al socket: %s\t", sock);

					if (ok) {
						printf("Esito: ok\n");
					}

					else {
						printf("Esito: errore\n");
					}
				}
				break;

			// imposto il tempo che intercorre tra l’invio di due richieste successive al server
			case 't':
				w = 0;
				r = 0;
				num = strtol(temp->arg, NULL, 0);

				// converto i msec inseriti dall'utente in secondi e nanosecondi per la nanosleep
				if (num > 1000) {
					sec = num/1000;
					msec = num % 1000;
				}
				
				else {
					msec = num;
				}

				tim1.tv_sec = sec;
				tim1.tv_nsec = msec * 1000000;

				// controllo se devo stampare su stdout
				if (print) {
					printf("\nt - Tempo fra due richieste: %ld ms\tEsito: ok\n", num);
				}
				break;

			// scrivi sul server 'n' file contenuti in una cartella
			case 'w':
				memset(Dir, '\0', 256);
				w = 1;
				r = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'D') {
						strncpy(Dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(Dir, 1) == -1) {
							if (print) {
								perror("-D");
							}
						}

						if (print) {
							printf("\nD - Cartella per le scritture del comando -w: %s\tEsito: ok\n", Dir);
						}
					}
				}

				cmd_w(temp->arg, Dir, print);

				break;

			// lista di nomi di file da scrivere nel server separati da virgole
			case 'W':
				memset(Dir, '\0', 256);
				w = 1;
				r = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'D') {
						strncpy(Dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(Dir, 1) == -1) {
							if (print) {
								perror("-D");
							}
						}

						if (print) {
							printf("\nD - Cartella per le scritture del comando -W: %s\tEsito: ok\n", Dir);
						}
					}
				}

				cmd_W(temp->arg, Dir, print);
				
				break;

			// imposta la cartella dove scrivere i file che il server rimuove a seguito di capacity misses in scrittura
			case 'D':
				// se il comando precedentemente non era una scrittura (-w o -W), errore
				if (!w) {
					printf("\nErrore: l'opzione -D deve essere usata congiuntamente all'opzione -w o -W.\n");
				}

				w = 0;
				r = 0;

				break;

			// leggi dal server una lista di file da leggere dal server, separati da virgole
			case 'r':
				memset(Dir, '\0', 256);
				r = 1;
				w = 0;

				// controllo se il prossimo comando specifica una directory nella quale salvare i file espulsi
				if (temp->next) {
					if ((temp->next)->cmd == 'd') {
						strncpy(dir, (temp->next)->arg, strlen((temp->next)->arg)+1);

						if (setDirectory(dir, 0) == -1) {
							if (print) {
								perror("-d");
							}
						}

						if (print) {
							printf("\nd - Cartella per le letture del comando -r: %s\tEsito: ok\n", dir);
						}
					}
				}

				cmd_r(temp->arg, dir, print);

				break;

			case 'd':
				// se il comando precedentemente non era una scrittura (-w o -W), errore
				if (!r) {
					printf("\nErrore: l'opzione -d deve essere usata congiuntamente all'opzione -r o -R.\n");
				}

				w = 0;
				r = 0;

				break;
		}

		temp = temp->next;

		// attendo prima di mandare la prossima richiesta al server
		nanosleep(&tim1, &tim2);
	}

	printf("\n");

	if (strcmp(sock, "") != 0) {
		closeConnection(sock);
	}
	return 0;
}

int destroyCmdList(cmdT *cmdList) {
	// controllo la validita' dell'argomento
	if (!cmdList) {
		errno = EINVAL;
		return -1;
	}

	cmdT *temp;

	// scorro tutta la lista e libero la memoria
	while (cmdList) {
		temp = cmdList;

		free(cmdList->arg);
		cmdList = cmdList->next;

		free(temp);
	}

	return 0;
}

// connettiti al socket specificato
int cmd_f(char* socket) {
	// controllo la validita' dell argomento
	if (!socket) {
		errno = EINVAL;
	}

	struct timespec ts;
	ts.tv_sec = 2;
	ts.tv_nsec = 550;

	errno = 0;
	if (openConnection(socket, 100, ts) != 0) {
		return -1;
	}

	return 0;
}

// scrivi sul server 'n' file contenuti in una cartella
int cmd_w(char *dirname, char *Directory, int print) {
	if (!dirname) {
		errno = EINVAL;
		return -1;
	}

	wT->maxN = 0;
	wT->currN = 0;
	char *token = NULL, *save = NULL;
	token = strtok_r(dirname, ",", &save);
	char Dir[256] = "";
	int cnt = 0;
	
	while (token != NULL && cnt < 2) {
		if (cnt == 0) {
			strncpy(Dir, token, strlen(token)+1);
		}

		// controlla se nel comando e' specificata l'opzione 'n'
		else if (cnt == 1) {
			if (strlen(token) > 2) {
				if (token[0] == 'n' && token[1] == '=') {
					char *number = token+2;
					for (int i = 0; i < strlen(number); i++) {
						if (!isdigit(number[i])) {
							errno = EINVAL;
							return -1;
						}
					}

					int num = (int) strtol(number, NULL, 0);
					if (num != 0) {
						wT->maxN = num;
					}
				}

				else {
					errno = EINVAL;
					return -1;
				}
			}
		}

		token = strtok_r(NULL, ",", &save);
		cnt++;
	}

	if (print) {
		wT->print = 2;
	}

	else {
		wT->print = 0;
	}

	strncpy(wT->Directory, Directory, strlen(Dir)+1);

	if (print) {
		printf("\nw - Scrivo i seguenti file sul server:");
		fflush(stdout);
	}

	ftw(Dir, cmd_w_aux, FOPEN_MAX);

	return 0;
}

// funzione ausiliaria del comando -w. Viene passata come argomento della funzione ftw
int cmd_w_aux(const char *ftw_filePath, const struct stat *ptr, int flag) {
	// controllo la validita' degli argomenti
	if (!ftw_filePath || !ptr) {
		errno = EINVAL;
		return -1;
	}

	if (flag != FTW_F) {
		return 0;
	}

	wT->currN++;

	if (wT->maxN != 0 && wT->currN > wT->maxN) {
		return -1;
	}

	if (cmd_W(ftw_filePath, wT->Directory, wT->print) == -1) {
		if (wT->print) {
			perror("-w");
		}
	}

	return 0;
}

// scrivi una lista di file sul server
int cmd_W(const char *filelist, char *Directory, int print) {
	// controllo la validita' degli argomenti
	if (!filelist || !Directory) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da scrivere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);
	int ok = 1;

	if (print == 1) {
		printf("\nW - Scrivo i seguenti file sul server:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		ok = 1;
		int opened = 0;
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		// apro il file
		if (openFile(token, 1) == -1) {
			// se il file esiste gia', rifai la openFile senza O_CREATE
			if (strcmp(strerror(errno), "File exists") == 0) {
				if (openFile(token, 0) == -1) {
					ok = 0;
				}
			}

			else {
				ok = 0;
			}
		}

		// se il file e' stato aperto con successo...
		if (ok) {
			opened = 1;
		}
		
		// scrivo il contenuto del file sul server
		if (ok && writeFile(token, Directory) == -1) {
			ok = 0;
		}

		// chiudo il file
		if (opened && closeFile(token) == -1) {
			ok = 0;
		}

		if (print != 0) {
			printf("Esito: "); 

			if (ok) {
				printf("ok");
			}
			
			else {
				printf("errore");

				if (print == 1) {
					perror("-W");
				}

				else {
					perror("-w");
				}
			}

			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	if (ok) {
		return 0;
	}

	else {
		return -1;
	}
}

int cmd_r(const char *filelist, char *directory, int print) {
	if (!filelist || !directory) {
		errno = EINVAL;
		return -1;
	}

	// parso la lista di file da leggere
	char *token = NULL, *save = NULL;
	char tokenList[256] = "";
	strncpy(tokenList, filelist, strlen(filelist)+1);
	token = strtok_r(tokenList, ",", &save);
	int ok = 1;

	if (print) {
		printf("\nr - Leggo i seguenti file dal server:\n");
		fflush(stdout);
	}

	// per ogni file nella lista...
	while (token != NULL) {
		ok = 1;
		int opened = 0;
		if (print != 0) {
			printf("\n%-20s", token); 
			fflush(stdout);
		}

		// apro il file
		if (openFile(token, 0) == -1) {
			ok = 0;
		}

		// se il file e' stato aperto con successo...
		else {
			opened = 1;
		}
		
		void *buf = NULL;
		size_t size = -1;

		// leggo il contenuto del file
		printInfo(0);
		if (ok && readFile(token, &buf, &size) == -1) {
			ok = 0;
		}
		printInfo(1);

		if (ok && print) {
			printf("Dimensione: %zu B\t", size);
			fflush(stdout);
		}

		if (buf) {
			free(buf);
		}

		// chiudo il file
		if (opened && closeFile(token) == -1) {
			ok = 0;
		}

		if (print) {
			printf("Esito: "); 

			if (ok) {
				printf("ok");
			}
			
			else {
				printf("errore");
				perror("-r");
			}

			printf("\n");
			fflush(stdout);
		}

		token = strtok_r(NULL, ",", &save);
	}

	if (ok) {
		return 0;
	}

	else {
		return -1;
	}
}