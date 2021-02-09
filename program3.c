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

void handle_SIGTSTP(int signo){
	char* message = "\nCaught SIGTSTP, Switching mode\n";
	// We are using write rather than printf
	write(STDOUT_FILENO, message, 32);

	if(strcmp(getenv("SIGTSTP_ACTION"), "1") == 0){
		message = "Exiting foreground-only mode.\n: ";
		write(STDOUT_FILENO, message, 31);
		setenv("SIGTSTP_ACTION", "0", 1);
	} else {
		message = "Entering foreground-only mode.\n: ";
		write(STDOUT_FILENO, message, 32);
		setenv("SIGTSTP_ACTION", "1", 1);
	}
}

void command_loop(){
	int shellPID = getpid();
	char* input;
	size_t size = 0;

	int exitLoop = -1;

	char* cwd = (char*)malloc(PATH_MAX*sizeof(char));
	getcwd(cwd, PATH_MAX*sizeof(char));


	int exitStatus = 0;		//-1 => there was some termination signal
	int terminationSignal = -1;


	size_t bgProcesses_size = 0;
	struct ProcessNode *head = NULL;

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

	do{
		input = (char*)malloc(2048*sizeof(char));

		printf(": ");
		fflush(stdout);

		int length;
		length = getline(&input, &size, stdin);
		fflush(stdin);
		input[length-1] = '\0';	//get the command inputed

		if(input[0] == '#' || strcmp(input, "") == 0){ 	//If it's blank or a comment
			reap_children(bgProcesses_size, &head, &exitStatus, &terminationSignal);
			continue;
		}


		expand_PID_var(input, shellPID);

		int argc = 0;
		char** argv = parse_words(input, &argc);
		struct Command *command = parse_command(argc, argv);


		if(strcmp(command->command, "exit") == 0){		//---exit---
			//TODO: Cleanup Background processes
			exitLoop = 0;
			free_command(command);
			continue;
		} else if(strcmp(command->command, "cd") == 0){		//---cd---
			char* directory;
			if(command->argc == 1){
				directory = getenv("HOME");
			} else {
				directory = command->argv[1];
			}
			int ret = chdir(directory);
			if(ret == 0){
				printf("Directory changed to %s\n", getcwd(cwd, PATH_MAX*sizeof(char) ));
				fflush(stdout);
			} else {
				printf("An error has occured.\n");
				fflush(stdout);
			}

		} else if(strcmp(command->command, "status") == 0){ 	//---status---
			//TODO: makesure this works correctly with the signals
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


					} else {
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
					if(command->execInBackground == 0 || strcmp(getenv(envVarName), "1") == 0){
						int childStatus;
						pid_t child = waitpid(childPID, &childStatus, 0);
						if(WIFEXITED(childStatus)){
							exitStatus = WEXITSTATUS(childStatus);
						} else if(WIFSIGNALED(childStatus)){
							exitStatus = -1;
							terminationSignal = WTERMSIG(childStatus);
							printf("Process Terminated with signal %d\n", terminationSignal);
							fflush(stdout);
						}
					} else {
						pid_t child = waitpid(childPID, &exitStatus, WNOHANG);
						printf("Background PID is %d\n", childPID);
						fflush(stdout);
						appendProcess(&head, childPID);

					}
			}
		}

		reap_children(bgProcesses_size, &head, &exitStatus, &terminationSignal);
		free_command(command);
		free(input);	




	} while (exitLoop < 0);


	freeLL(head);
}


void reap_children(int process_number, struct ProcessNode **processes, int *exitStatus, int *terminationSignal){
	struct ProcessNode *curr = *processes;
	while(curr != NULL){		
		int childStatus;
		pid_t child = waitpid(curr->pid, &childStatus, WNOHANG);
		if(child != 0){
			if(WIFEXITED(childStatus)){
				printf("Process with pid %d has ended.\n",curr->pid);
				fflush(stdout);
				int currPID = curr->pid;
				curr = curr->next;
				removeProcess(processes, currPID);
				*exitStatus = WEXITSTATUS(childStatus);
			} else if(WIFSIGNALED(childStatus)){
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
		if(strcmp(args[i], "<") == 0){
			command->useInputFile = 1;
			i++;
			command->inputFile = args[i];

		} else if(strcmp(args[i], ">") == 0){
			command->useOutputFile = 1;
			i++;
			command->outputFile = args[i];

		} else if(strcmp(args[i], "&") == 0&& i == argc-1){
			command->execInBackground = 1;
		} else {
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
 *
 * Return: 
 * 	Array of pointers to each word in the input string
 */
char** parse_words(char* string, int *count){

	char** output = (char**)malloc(sizeof(char*));

	*count = 0;
	char* token, *saveptr;
	int i;
	for(token = strtok_r(string, " ", &saveptr), i = 0; token != NULL; token = strtok_r(NULL, " ", &saveptr), i++){
		(*count)++;
		output = (char**)realloc(output, (*count)*sizeof(char*));
		output[i] = token;
	}
	return output;

}

/* utility function for counting words in a string
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
	char* tmp = malloc(1);
	char* str = command;
	while((str = strstr(str, "$$"))){
		tmp = realloc(tmp, str-command+1);
		tmp = strncpy(tmp, command, str-command);
		tmp[str-command] = '\0';
		tmp = realloc(tmp, (strlen(tmp)+strlen(PIDc)));
		length += strlen(PIDc);
		strcat(tmp, PIDc);
		strcat(tmp, str+strlen("$$"));
		command = (char*)realloc(command, (length+strlen(PIDc))*sizeof(char));
		length += strlen(PIDc);
		strcpy(command, tmp);
	}
	free(PIDc);
	free(tmp);
}

void free_command(struct Command *command){
	free(command->argv);
	free(command);

}

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


void freeLL(struct ProcessNode* head){
	if(head == NULL){
		return;
	}
	if(head->next != NULL){
		freeLL(head->next);
	}
	free(head);
}

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
