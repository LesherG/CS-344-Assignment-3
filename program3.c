#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/wait.h>
#include <fcntl.h>

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


void command_loop();
void reap_children(int process_number, struct ProcessNode **processes, int *exitStatus);
struct Command* parse_command(int argc, char** args);
char** parse_words(char* string, int count);
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

void command_loop(){
	int shellPID = getpid();
	char* input = (char*)malloc(2048*sizeof(char));
	size_t size;

	int exitLoop = -1;

	char* cwd = (char*)malloc(PATH_MAX*sizeof(char));
	getcwd(cwd, PATH_MAX*sizeof(char));

	
	int exitStatus = 0;		//-1 => there was some termination signal
	int terminationSignal = -1;


	size_t bgProcesses_size = 0;
	struct ProcessNode *head = NULL;

	do{
		printf(": ");
		fflush(stdout);

		getline(&input, &size, stdin);
		input[strlen(input)-1] = '\0';	//get the command inputed

		if(input[0] == '#' || strcmp(input, "") == 0){ 	//If it's blank or a comment
			reap_children(bgProcesses_size, &head, &exitStatus);
			continue;
		}


		expand_PID_var(input, shellPID);

		int argc = count_words(input);
		char** argv = parse_words(input, argc);
		struct Command *command = parse_command(argc, argv);

		if(strcmp(command->command, "exit") == 0){		//---exit---
			//TODO: Cleanup Background processes
			exitLoop = 0;
			free_command(command);
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
			} else {
				printf("An error has occured.\n");
			}

		} else if(strcmp(command->command, "status") == 0){ 	//---status---
			//TODO: makesure this works correctly with the signals
			if(exitStatus == -1){
				printf("Terminated by signal %d\n", terminationSignal);
			} else {
				printf("Exit value %d\n", exitStatus);
			}
		} else if(strcmp(command->command, "proc") == 0){
			printProcesses(head);
		} else {						//---Other Commands---
			pid_t childPID = fork();	
			switch(childPID){
				case -1:
					perror("fork() failed. Hull breach.\n");
					exit(1);
					break;
				case 0:
					//--setup file redirects--
					
					if(command->execInBackground == 1){
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
					}

					if(command->useInputFile == 1){
						int sourceFD = open(command->inputFile, O_RDONLY);
						if(sourceFD == -1){
							perror("Input file redirection failed, exiting\n");
							exit(1);
						}
						int result = dup2(sourceFD, 0); 	//Redirect stdin to source file
						if(result == -1) {
							perror("input dup2 failed, exiting\n");
							exit(1);
						}
					}
					if(command->useOutputFile == 1){
						int targetFD = open(command->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
						if(targetFD == -1){
							perror("Output file redirection failed, exiting\n");	
							exit(1);
						}
						int result = dup2(targetFD, 1);		//Redirect stdout to target file
						if(result == -1){

							perror("output dup2 failed, exiting\n");
							exit(1);
						}
					}

					//--execute commands---
					execvp(command->command, command->argv);
					printf("Failed to execute command.\n");
					exit(1);
					break;
				default:
					if(command->execInBackground == 0){
						int childStatus;
						pid_t child = waitpid(childPID, &childStatus, 0);
						if(WIFEXITED(childStatus)){
							exitStatus = WEXITSTATUS(childStatus);
						}
					} else {
						pid_t child = waitpid(childPID, &exitStatus, WNOHANG);
						printf("Background PID is %d\n", childPID);
						appendProcess(&head, childPID);
						
					}
			}
		}

		reap_children(bgProcesses_size, &head, &exitStatus);




	} while (exitLoop < 0);


	free(input);	
	freeLL(head);
}


void reap_children(int process_number, struct ProcessNode **processes, int *exitStatus){
	struct ProcessNode *curr = *processes;
	while(curr != NULL){		
		int childStatus;
		pid_t child = waitpid(curr->pid, &childStatus, WNOHANG);
		if(child != 0){
			printf("Process with pid %d has ended.\n",curr->pid);
			int currPID = curr->pid;
			curr = curr->next;
			removeProcess(processes, currPID);
			*exitStatus = WEXITSTATUS(childStatus);

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

		} else if(strcmp(args[i], "&") == 0){
			command->execInBackground = 1;
		} else {
			command->argc++;
			command->argv = (char**)realloc(command->argv, (command->argc)*sizeof(char*));
			command->argv[command->argc-1] = args[i];		
		}

	}	
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
char** parse_words(char* string, int count){

	char** output = malloc((count)*sizeof(char*));

	char* token, *saveptr;
	int i;
	for(token = strtok_r(string, " ", &saveptr), i = 0; token != NULL; token = strtok_r(NULL, " ", &saveptr), i++){
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
	char* tmp = (char*)malloc(length*sizeof(char));
	char* str = command;
	while((str = strstr(str, "$$"))){
		strncpy(tmp, command, str-command);
		tmp[str-command] = '\0';
		tmp = (char*)realloc(tmp, (length+strlen(PIDc))*sizeof(char));
		strcat(tmp, PIDc);
		strcat(tmp, str+strlen("$$"));
		command = (char*)realloc(command, (length+strlen(PIDc))*sizeof(char));
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
