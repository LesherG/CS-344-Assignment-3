#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

struct Command {
	char* command;
	int argc;
	char** argv;

	int useInputFile;
	char* inputFile;

	int useOutputFile;
	char* outputFile;

	int execInBackground;
};

struct ProcessNode{
	struct ProcessNode* next;
	struct ProcessNode* prev;

	int pid;
};


void handle_SIGINT(int signo);
void command_loop();
void reap_children(int process_number, struct ProcessNode **processes, int *exitStatus, int *terminationSignal);
struct Command* parse_command(int argc, char** args);
char** parse_words(char* string, int *count);
int count_words(char* string);
void expand_PID_var(char* command, int PID);
void free_command(struct Command *command);
//---LL functions---
void appendProcess(struct ProcessNode **head, int pid);
void removeProcess(struct ProcessNode **head, int pid);
void freeLL(struct ProcessNode* head);
void printProcesses(struct ProcessNode* head);

int main(int argc, char** argv){
	command_loop();
}

/* Func to handle SIGTSTP signal
 *
 * changes environment variable relating to the foreground state
 *
 */
void handle_SIGTSTP(int signo){
	char* message = "\nCaught SIGTSTP, Switching mode\n";
	// We are using write rather than printf
	write(STDOUT_FILENO, message, 32);

	if(strcmp(getenv("SIGTSTP_ACTION"), "1") == 0){ 	//---Exit FG only mode---
		message = "Exiting foreground-only mode.\n: ";
		write(STDOUT_FILENO, message, 31);
		setenv("SIGTSTP_ACTION", "0", 1);
	} else {						//---Enter FG only mode---
		message = "Entering foreground-only mode.\n: ";
		write(STDOUT_FILENO, message, 32);
		setenv("SIGTSTP_ACTION", "1", 1);
	}
}

/* Main command loop of smallsh.
 *	
 * Does most command management, and fork/exec
 * Portions will be marked
 */
void command_loop(){
	//---VAR SETUP---
	int shellPID = getpid();
	char* input;
	size_t size = 0;

	int exitLoop = -1;

	//---CD UTILITY---
	char* cwd = (char*)malloc(PATH_MAX*sizeof(char));
	getcwd(cwd, PATH_MAX*sizeof(char));

	//---STATUS UTILITY---
	int exitStatus = 0;		//-1 => there was some termination signal
	int terminationSignal = -1;


	//---KEEPING TRACK OF BG PROCESSES---
	size_t bgProcesses_size = 0;
	struct ProcessNode *head = NULL;

	//---SIGTSTP AND SIGINT HANDLING---
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);

	SIGTSTP_action.sa_flags = SA_RESTART;

	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	struct sigaction ignore_action = {0};
	ignore_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &ignore_action, NULL);
	

	char* envVarName = "SIGTSTP_ACTION";
	setenv(envVarName, "0", 1); 		//1 = ignore background stuff
	
	//---MAIN COMMAND LOOP---
	do{
		input = (char*)malloc(2048);

		printf(": ");
		fflush(stdout);

		int length;
		length = getline(&input, &size, stdin);
		fflush(stdin);
		input[length-1] = '\0';	//get the command inputed

		if(input[0] == '#' || strcmp(input, "") == 0){ 	//If it's blank or a comment
			reap_children(bgProcesses_size, &head, &exitStatus, &terminationSignal);
			free(input);
			continue;
		}


		expand_PID_var(input, shellPID);

		int argc = 0;
		char** argv = parse_words(input, &argc);
		struct Command *command = parse_command(argc, argv);


		if(strcmp(command->command, "exit") == 0){		//---exit---
			struct ProcessNode *curr = head; 		//--Send a kill signal to all processes opened by shell--
		       	while(curr != NULL){				//SIGKILL for an uninterruptable signal
				kill(curr->pid, SIGKILL);
				curr = curr->next;
			}	
			exitLoop = 0;
			free(input);
			free_command(command);
			free(argv);
			continue;
		} else if(strcmp(command->command, "cd") == 0){		//---cd---
			char* directory;
			if(command->argc == 1){				//if there's no args
				directory = getenv("HOME");
			} else {
				directory = command->argv[1];
			}
			int ret = chdir(directory);
			if(ret == 0){					//if the change was successful
				printf("Directory changed to %s\n", getcwd(cwd, PATH_MAX*sizeof(char) ));
				fflush(stdout);
			} else {
				printf("An error has occured.\n");
				fflush(stdout);
			}

		} else if(strcmp(command->command, "status") == 0){ 	//---status---
			if(exitStatus == -1){
				printf("Terminated by signal %d\n", terminationSignal);
				fflush(stdout);
			} else {
				printf("Exit value %d\n", exitStatus);
				fflush(stdout);
			}
		} else if(strcmp(command->command, "proc") == 0){
			printProcesses(head);
		} else {						//---Other Commands---
			pid_t childPID = fork();	
			switch(childPID){
				case -1:
					perror("fork() failed. Hull breach.\n");
					fflush(stdout);
					exit(1);
					break;
				case 0:
					SIGTSTP_action.sa_handler = SIG_IGN; //children ignore SIGTSTP
					sigaction(SIGTSTP, &SIGTSTP_action, NULL);

					//--setup file redirects--

					//--File redirect overrides for bacground processes--
					if(command->execInBackground == 1 && strcmp(getenv(envVarName), "0") == 0){
						if(command->useInputFile == 0){
							command->inputFile = (char*)malloc(20*sizeof(char));
							strcpy(command->inputFile, "/dev/null");
							command->useInputFile = 1;
						}
						if(command->useOutputFile == 0){
							command->outputFile = (char*)malloc(20*sizeof(char));
							strcpy(command->outputFile, "/dev/null");
							command->useOutputFile = 1;
						}


					} else { 	//Bring back response for SIGINT on fg processes
						ignore_action.sa_handler = SIG_DFL;
						sigaction(SIGINT, &ignore_action, NULL);
					}

					if(command->useInputFile == 1){
						int sourceFD = open(command->inputFile, O_RDONLY);
						if(sourceFD == -1){
							perror("Input file redirection failed, exiting\n");
							fflush(stdout);
							exit(1);
						}
						int result = dup2(sourceFD, 0); 	//Redirect stdin to source file
						if(result == -1) {
							perror("input dup2 failed, exiting\n");
							fflush(stdout);
							exit(1);
						}
					}
					if(command->useOutputFile == 1){
						int targetFD = open(command->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
						if(targetFD == -1){
							perror("Output file redirection failed, exiting\n");	
							fflush(stdout);
							exit(1);
						}
						int result = dup2(targetFD, 1);		//Redirect stdout to target file
						if(result == -1){

							perror("output dup2 failed, exiting\n");
							fflush(stdout);
							exit(1);
						}
					}

					//--execute commands---
					int err = execvp(command->command, command->argv);
					printf("Failed to execute command. error: %s\n", strerror(errno));
					fflush(stdout);
					exit(1);
					break;
				default:
					if(command->execInBackground == 0 || strcmp(getenv(envVarName), "1") == 0){ 	//if fg only mode is on, or it's a fg process
						int childStatus;
						pid_t child = waitpid(childPID, &childStatus, 0); 			//wait for it
						if(WIFEXITED(childStatus)){						//set exit status
							exitStatus = WEXITSTATUS(childStatus);
						} else if(WIFSIGNALED(childStatus)){					//or termination status
							exitStatus = -1;
							terminationSignal = WTERMSIG(childStatus);
							printf("Process Terminated with signal %d\n", terminationSignal);
							fflush(stdout);
						}
					} else {								//if it's a bg process
						pid_t child = waitpid(childPID, &exitStatus, WNOHANG);		//don't wait for it
						printf("Background PID is %d\n", childPID);
						fflush(stdout);
						appendProcess(&head, childPID);					//add it to list of bg processes

					}
			}
		}

		reap_children(bgProcesses_size, &head, &exitStatus, &terminationSignal);
		free_command(command);
		free(input);	
		free(argv);
	} while (exitLoop < 0);


	freeLL(head);
	free(cwd);
}


/* Reaps children of the larger shell parent process
 *
 * Args:
 * 	process_number: number of processes (unused)
 * 	processes: Linked list of the background processes
 * 	exitStatus: adress of var to be set as exit status
 * 	terminationSignal: adress of var to be set as termination signal, if needed
 */
void reap_children(int process_number, struct ProcessNode **processes, int *exitStatus, int *terminationSignal){
	struct ProcessNode *curr = *processes;
	while(curr != NULL){				//For every node		
		int childStatus;
		pid_t child = waitpid(curr->pid, &childStatus, WNOHANG);
		if(child != 0){				//if the child was reaped
			if(WIFEXITED(childStatus)){ 	//update exitstatus
				printf("Process with pid %d has ended with exit value %d.\n",curr->pid, WEXITSTATUS(childStatus));
				fflush(stdout);
				int currPID = curr->pid;
				curr = curr->next;
				removeProcess(processes, currPID);
				*exitStatus = WEXITSTATUS(childStatus);
			} else if(WIFSIGNALED(childStatus)){ //or update term signal
				printf("Process with pid %d has ended. Terminated by signal: %d\n",curr->pid, WTERMSIG(childStatus));
				fflush(stdout);
				int currPID = curr->pid;
				curr = curr->next;
				removeProcess(processes, currPID);
				*exitStatus = -1;
				*terminationSignal = WTERMSIG(childStatus);
			}

		} else {
			curr = curr->next;
		}	
	}
}

/* Parses array of arguements into command struct
 * 
 * Args:
 * 	argc: number of arguements
 * 	argv: array of arguements
 */
struct Command* parse_command(int argc, char** args){
	struct Command *command = (struct Command*)malloc(sizeof(struct Command));

	command->command = args[0];
	command->useInputFile = 0;
	command->useOutputFile = 0;
	command->execInBackground = 0;

	command->argc = 1;
	command->argv = (char**)malloc(sizeof(char*));
	command->argv[0] = args[0];

	int i = 1;
	for( ; i < argc; i++){ 
		if(strcmp(args[i], "<") == 0){ 		//Input file managament
			command->useInputFile = 1;
			i++;
			command->inputFile = args[i];

		} else if(strcmp(args[i], ">") == 0){	//output file management
			command->useOutputFile = 1;
			i++;
			command->outputFile = args[i];

		} else if(strcmp(args[i], "&") == 0&& i == argc-1){//bg process
			command->execInBackground = 1;
		} else {				//all other args
			command->argc++;
			command->argv = (char**)realloc(command->argv, argc*(sizeof(char*)));
			command->argv[command->argc-1] = args[i];		
		}

	}
	command->argv = (char**)realloc(command->argv, (command->argc+1)*(sizeof(char*)));
	command->argv[command->argc] = NULL;
	return command;

}

/* Parses the "words" from a command line string.
 * Uses " " as a delimiter
 *
 * DOES NOT MAKE A COPY
 * replaces spaces in original string with "\0" characters
 * 
 * Args:
 * 	string: character string to be parsed for words
 * 	count: return value for number of words
 *
 * Return: 
 * 	Array of pointers to each word in the input string
 */
char** parse_words(char* string, int *count){

	char** output = (char**)malloc(512*sizeof(char*));

	*count = 0;
	char* token, *saveptr;
	int i;
	for(token = strtok_r(string, " ", &saveptr), i = 0; token != NULL; token = strtok_r(NULL, " ", &saveptr), i++){
		(*count)++;
		//output = realloc(output, (*count)*sizeof(char*));
		output[i] = token;
	}
	return output;

}

/* UNUSED FUNCTION
 *
 * utility function for counting words in a string
 *
 * Args:
 * 	string: input string for words to be counted
 *
 * return: 
 * 	number of words (Spaces + 1) 
 *
 */
int count_words(char* string){
	int i = 0;
	int count = 0;
	for( ; i < strlen(string); i++){
		if(string[i] == ' '){
			count++;
		}
	}
	return count+1;
}

/* Adapted from https://stackoverflow.com/questions/32413667/replace-all-occurrences-of-a-substring-in-a-string-in-c
 * 
 * Expands every instance of "$$" into the pid provided
 * Args:
 * 	command: full command string
 * 	PID: parent PID
 */
void expand_PID_var(char* command, int PID){
	int count, n;
	n = PID;
	count = 0;
	while(n != 0){
		n /= 10;
		count++;
	}
	count++;
	char* PIDc = (char*)malloc(count*sizeof(char));
	sprintf(PIDc, "%d", PID);

	int length = strlen(command);
	char* tmp = malloc(2048);
	char* str = command;
	while((str = strstr(str, "$$"))){
		//tmp = realloc(tmp, str-command+1);
		tmp = strncpy(tmp, command, str-command);
		tmp[str-command] = '\0';
		//tmp = realloc(tmp, (strlen(tmp)+strlen(PIDc)));
		length += strlen(PIDc);
		strcat(tmp, PIDc);
		strcat(tmp, str+strlen("$$"));
		//command = realloc(command, (length+strlen(PIDc)-2));
		length += strlen(PIDc);
		strcpy(command, tmp);
	}
	free(PIDc);
	free(tmp);
}

/* Frees the command struct passed as arguement
 */
void free_command(struct Command *command){
	free(command->argv);
	free(command);

}

/* Appends Node to end of LL
 *
 * Args:
 * 	head: adress of head of linked list
 * 	pid: node data to be appended
 */
void appendProcess(struct ProcessNode **head, int pid){
	if(*head == NULL){
		struct ProcessNode *newNode = (struct ProcessNode*)malloc(sizeof(struct ProcessNode));
		newNode->prev = NULL;
		newNode->next = NULL;
		newNode->pid = pid;
		*head = newNode;
		return;
	} else {
		struct ProcessNode *curr = *head;
		while(curr->next != NULL){
			curr = curr->next;
		}
		struct ProcessNode *newNode = (struct ProcessNode*)malloc(sizeof(struct ProcessNode));
		newNode->prev = curr;
		newNode->next = NULL;
		newNode->pid = pid;
		curr->next = newNode;
	}
}

/* Removes pid specified from linked list
 *
 * Args:
 * 	head: adress of head of linked list
 * 	pid: node data to be deleted
 */
void removeProcess(struct ProcessNode **head, int pid){
	struct ProcessNode *curr = *head;
	if(curr != NULL && curr->pid == pid){
		if(curr->next){
			curr->next->prev = NULL;
		}
		*head = curr->next;
		free(curr);
		return;
	}

	while(curr != NULL && curr->pid != pid){
		curr = curr->next;
	}

	curr->prev->next = curr->next;
	curr->next->prev = curr->prev;

	free(curr);

}

/* Frees entire linked list recursively given the head
 */
void freeLL(struct ProcessNode* head){
	if(head == NULL){
		return;
	}
	if(head->next != NULL){
		freeLL(head->next);
	}
	free(head);
}

/* Prints processes in the Process linked list
 */
void printProcesses(struct ProcessNode* head){
	printf("Processes: \n");
	if(head == NULL){
		return;
	}
	struct ProcessNode* curr = head;
	while(curr != NULL){
		printf("pid: %d\n", curr->pid);
		curr = curr->next;
	}
}
