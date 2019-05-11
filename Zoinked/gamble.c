#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <mpi.h>


//magic numbers to abuse the c rng
//num players = 64
//money range = 10-20

void play(int buff[4], int * winner, int * money);

int gamble(int argc, char* argv[]) {
	MPI_Init(&argc, &argv);

	int rank; //rank of the current process
	int numprocs; //number of processes running in parallel
	int numplayers = 0; //total number of players
	int numrounds = 0; //number of rounds is useful for determining how many iterations to go through
	int sendcount = 4; //always send 4 ints (2 players)
	int recvcount = 4; //always receive 4 ints (2 players)
	int root = 0; //root id is important for all procs (for send and receive routines)

	//get rank and number of processes
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &numprocs);

	//print the number of processes
	if (rank == 0) {
		printf("numprocs : %d \n", numprocs);
		fflush(stdout);
	}

	//seed the random for all processes
	srand((unsigned int)time(NULL));

	//generate a random number of players
	numplayers = (rand() % 6) + 5; // 5-10 players

	//numplayers = 21; // to hardcode the number of players

	//to pass number of players as command-line argument
	if (argc > 1) {
		numplayers = atoi(argv[1]);
	}

	//print number of players
	if (rank == 0) {
		printf("numplayers : %d \n", numplayers);
		fflush(stdout);
	}

	//dont fill the send buffer immediately as only the root process needs it
	int *playerbuffer = NULL;

	//the receive buffer is always 4 integers (2 players)
	int recvbuffer[4];

	//error if number of players is less than 0
	if (numplayers < 1 && rank == 0) {
		printf("invalid player number\n");
		fflush(stdout);
		MPI_Finalize();
		return 0;
	}
	else if (numplayers < 1 && rank != 0) {
		MPI_Finalize();
		return 0;
	}

	//automatic win if the number of players is 1
	if (numplayers == 1 && rank == 0) {
		printf("player 0 is the winner by default as there are no other players\n");
		fflush(stdout);
		MPI_Finalize();
		return 0;
	}
	else if (numplayers == 1 && rank != 0) {
		MPI_Finalize();
		return 0;
	}

	//calculate number of rounds
	for (int left = numplayers; left > 1; numrounds++) {
		if (left % 2 == 0) {
			left /= 2;
		}
		else {
			left /= 2;
			left++;
		}
	}

	//generate random player data (random money)
	if (rank == 0) {
		//allocate the send buffer now that we know how many players there are in total
		playerbuffer = malloc(numplayers * 2 * sizeof(int));

		//fill the buffer with player id and player money
		for (int i = 0; i < numplayers; i++) {
			playerbuffer[2 * i] = i; // player id
			playerbuffer[2 * i + 1] = (rand() % 11) + 10; // 10-20 money
		}

		//playbuffer[(numplayers - 1) * 2 + 1] = 20;

		//print the initial player data
		printf("\nInitial player data : \n");
		for (int i = 0; i < numplayers; i++) {
			printf("%d %d \n", playerbuffer[2 * i], playerbuffer[2 * i + 1]);
		}
		fflush(stdout);
	}

	//remaining players
	int numplayersleft = numplayers;
	int * playersleft = NULL;
	int * scores = NULL;

	//fill the remaining players buffer with all the players initially
	if (rank == 0) {
		playersleft = malloc(numplayers * 2 * sizeof(int));
		for (int i = 0; i < numplayers; i++) {
			playersleft[2 * i] = playerbuffer[2 * i];
			playersleft[2 * i + 1] = playerbuffer[2 * i + 1];
		}
	}

	//start the main calculation loop
	for (int i = 0; i < numrounds; i++) {
		if (rank == 0) {
			printf("\nround %d\n", i + 1);
			fflush(stdout);
		}

		//calculate how many games are in the current round
		int numgames = numplayersleft / 2;

		//case of round having even number of players
		if (numplayersleft % 2 == 0) {
			
			//root preparations for scattering data
			if (rank == 0) {
				scores = malloc(numgames * 2 * sizeof(int));
			}

			//number of games left this round
			int gamesleft = numgames;
			
			//offset tells us where in the send and receive buffers to start
			int offset = 0;

			//first split games across all procs
			while (gamesleft >= numprocs) {

				MPI_Scatter(playersleft + offset * 2, sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, MPI_COMM_WORLD);

				//play code
				int winbuff[2];
				play(recvbuffer, winbuff, winbuff + 1);
				printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
				fflush(stdout);

				//organize results
				MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, MPI_COMM_WORLD);

				//increase offset
				offset += numprocs * 2;
				
				//decrease number of games remaining
				gamesleft -= numprocs;
			}

			MPI_Barrier(MPI_COMM_WORLD);

			//then split remaining games acrss all procs that are needed
			if (gamesleft > 0) {

				MPI_Comm subcomm;
				int color = (rank < gamesleft) ? -1 : 1; //procs with -1 color will be in the communicator that we need
				MPI_Comm_split(MPI_COMM_WORLD, color, rank, &subcomm);

				/*if (rank == 0) {
					printf("games left : %d\n", gamesleft);
					fflush(stdout);
				}*/

				if (rank < gamesleft) {
					/*printf("proc %d will be in the new comm", rank);
					fflush(stdout);*/

					//Create communicator
					MPI_Scatter(playersleft + offset * 2, sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, subcomm);

					int winbuff[2];
					play(recvbuffer, winbuff, winbuff + 1);
					printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
					fflush(stdout);

					MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, subcomm);
					
					/*offset += gamesleft * 2;
					gamesleft = 0;*/
				}

				//release allocated data
				MPI_Comm_free(&subcomm);

				//wait all procs before proceeding as not all procs are working here
				MPI_Barrier(MPI_COMM_WORLD);
			}

			//finished all game rounds and received the winners now create new remaining players array in the correct order
			free(playersleft);

			if (rank == 0) {
				playersleft = scores;
			}

			numplayersleft /= 2;

		}

		//case of round having odd number of players (top player wins by default)
		else {

			//find the player with the most money
			int topplayer = 0;
			int topid = 0;
			int topscore = 0;

			if (rank == 0) {
				topid = playersleft[0];
				topscore = playersleft[1];
				for (int i = 0; i < numplayersleft; i++) {
					if (playersleft[2 * i + 1] > topscore) {
						topscore = playersleft[2 * i + 1];
						topid = playersleft[2 * i];
						topplayer = i;
					}
				}
			}

			//broadcast the place of the top player so other procs can calculate the offsets
			MPI_Bcast(&topplayer, 1, MPI_INT, root, MPI_COMM_WORLD);
			
			if (rank == 0) {
				printf("top player : %d with score %d \n", topid, topscore);
				fflush(stdout);
			}

			//allocate score data
			if (rank == 0) {
				scores = malloc((numgames + 1) * 2 * sizeof(int));
			}

			//the top player is in an even spot so we split the games into two blocks only
			if (topplayer % 2 == 0) {

				int gamesleft = topplayer / 2;
				int offset = 0;

				//2 passes
				for (int pass = 0; pass < 2; pass++) {

					while (gamesleft >= numprocs) {

						/*printf("%d : everyone over here\n", rank);
						fflush(stdout);*/

						MPI_Scatter(playersleft + offset * 2 - (pass * 2), sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, MPI_COMM_WORLD);

						//play code
						int winbuff[2];
						play(recvbuffer, winbuff, winbuff + 1);
						printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
						fflush(stdout);

						//organize results
						MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, MPI_COMM_WORLD);

						//increase offset
						offset += numprocs * 2;

						//decrease number of games remaining
						gamesleft -= numprocs;
					}

					MPI_Barrier(MPI_COMM_WORLD);

					//then split remaining games acrss all procs that are needed
					if (gamesleft > 0) {

						MPI_Comm subcomm;
						int color = (rank < gamesleft) ? -1 : 1; //procs with -1 color will be in the communicator that we need
						MPI_Comm_split(MPI_COMM_WORLD, color, rank, &subcomm);

						/*if (rank == 0) {
							printf("games left : %d\n", gamesleft);
							fflush(stdout);
						}*/

						if (rank < gamesleft) {
							/*printf("proc %d will be in the new comm", rank);
							fflush(stdout);*/

							//Create communicator
							MPI_Scatter(playersleft + offset * 2 - (pass * 2), sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, subcomm);

							int winbuff[2];
							play(recvbuffer, winbuff, winbuff + 1);
							printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
							fflush(stdout);

							MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, subcomm);

							offset += gamesleft * 2;
							//gamesleft = 0;
						}

						//release allocated data
						MPI_Comm_free(&subcomm);

						//wait all procs before proceeding as not all procs are working here
						MPI_Barrier(MPI_COMM_WORLD);
					}

					//intervention between 2 passes
					if (pass == 0) {

						if (rank == 0) {
							//auto win for the top player
							*(scores + offset) = topid;;
							*(scores + offset + 1) = topscore;

							printf("%d : player %d with %d money wins by default\n", rank, topid, topscore);
							fflush(stdout);
						}

						//adjust offset accordingly
						offset += 2;
						gamesleft = (numplayersleft - topplayer) / 2;
					}
				}

			}

			//the top player is in an odd spot so we split the games into two blocks
			//we also handle the special case of the two players closest to the top player
			else {

				int gamesleft = (topplayer - 1) / 2;

				int offset = 0;

				//2 passes
				for (int pass = 0; pass < 2; pass++) {

					while (gamesleft >= numprocs) {

						MPI_Scatter(playersleft + offset * 2 - (pass * 2), sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, MPI_COMM_WORLD);

						//play code
						int winbuff[2];
						play(recvbuffer, winbuff, winbuff + 1);
						printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
						fflush(stdout);

						//organize results
						MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, MPI_COMM_WORLD);

						//increase offset
						offset += numprocs * 2;

						//decrease number of games remaining
						gamesleft -= numprocs;
					}

					MPI_Barrier(MPI_COMM_WORLD);

					//then split remaining games acrss all procs that are needed
					if (gamesleft > 0) {

						MPI_Comm subcomm;
						int color = (rank < gamesleft) ? -1 : 1; //procs with -1 color will be in the communicator that we need
						MPI_Comm_split(MPI_COMM_WORLD, color, rank, &subcomm);

						/*if (rank == 0) {
							printf("games left : %d\n", gamesleft);
							fflush(stdout);
						}*/

						if (rank < gamesleft) {
							/*printf("proc %d will be in the new comm", rank);
							fflush(stdout);*/

							//Create communicator
							MPI_Scatter(playersleft + offset * 2 - (pass * 2), sendcount, MPI_INT, recvbuffer, recvcount, MPI_INT, root, subcomm);

							int winbuff[2];
							play(recvbuffer, winbuff, winbuff + 1);
							printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
							fflush(stdout);

							MPI_Gather(&winbuff, 2, MPI_INT, scores + offset, 2, MPI_INT, root, subcomm);

							offset += gamesleft * 2;
							//gamesleft = 0;
						}

						//release allocated data
						MPI_Comm_free(&subcomm);

					}

					//wait all procs before proceeding as not all procs are working here
					MPI_Barrier(MPI_COMM_WORLD);

					//intervention between 2 passes
					if (pass == 0) {

						if (rank == 0) {

							//the two players surrounding the top player will play against each other
							int winbuff[2];
							recvbuffer[0] = playersleft[topplayer * 2 - 2];
							recvbuffer[1] = playersleft[topplayer * 2 - 1];
							recvbuffer[2] = playersleft[topplayer * 2 + 2];
							recvbuffer[3] = playersleft[topplayer * 2 + 3];
							play(recvbuffer, winbuff, winbuff + 1);

							//the player before the top player won
							if (winbuff[0] == recvbuffer[0]) {
								//the winning player
								*(scores + offset) = winbuff[0];
								*(scores + offset + 1) = winbuff[1];

								printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
								fflush(stdout);

								//auto win for top player
								*(scores + offset + 2) = topid;
								*(scores + offset + 3) = topscore;

								printf("%d : player %d with %d money wins by default\n", rank, topid, topscore);
								fflush(stdout);
							}
							//the player after the top player won
							else {
								//auto win for top player
								*(scores + offset) = topid;
								*(scores + offset + 1) = topscore;
								
								printf("%d : player %d with %d money wins by default\n", rank, topid, topscore);
								fflush(stdout);

								//the winning player
								*(scores + offset + 2) = winbuff[0];
								*(scores + offset + 3) = winbuff[1];

								printf("%d : player %d won with %d money \n", rank, winbuff[0], winbuff[1]);
								fflush(stdout);
							}
						}

						//adjust offset for second pass
						offset += 4;
						gamesleft = (numplayersleft - topplayer - 1) / 2;

						MPI_Barrier(MPI_COMM_WORLD);
					}

				}

			}

			//finished all game rounds and received the winners now create new remaining players array in the correct order
			free(playersleft);

			if (rank == 0) {
				playersleft = scores;
			}

			numplayersleft /= 2;
			numplayersleft++;
		}

	}

	MPI_Barrier(MPI_COMM_WORLD);
	
	//at the end there should be only one player left in the player buffer
	if (rank == 0) {
		printf("\nplayer %d is the final winner with %d money \n", playersleft[0], playersleft[1]);
		fflush(stdout);
	}

	MPI_Finalize();
	return 0;
}

void play(int buff[4], int * winner, int * money) {
	int p1 = buff[1];
	int p2 = buff[3];

	while (p1 > 0 && p2 > 0) {
		int coin = rand() % 2;
		if (coin) {
			p1++;
			p2--;
		}
		else {
			p1--;
			p2++;
		}
	}

	if (p1 == 0) {
		*winner = buff[2];
		*money = buff[1] + buff[3];
	}
	else {
		*winner = buff[0];
		*money = buff[1] + buff[3];
	}
}
