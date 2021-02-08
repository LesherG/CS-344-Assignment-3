#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

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

void command_loop();
struct Command* parse_command(int argc, char** argv);
char** parse_words(char* string, int count);
int count_words(char* string);
void expandPIDVar(char* command, int PID);
void free_command(struct Command *command);

int main(int argc, char** argv){
	command_loop();
}

void command_loop(){
	int shellPID = getpid();
	char* input = (char*)malloc(2048*sizeof(char));
	size_t size;
	int exitStatus = -1;

	do{
		printf(": ");
		fflush(stdout);

		getline(&input, &size, stdin);
		input[strlen(input)-1] = '\0';	//get the command inputed

		if(input[0] == '#' || strcmp(input, "") == 0){ 	//If it's blank or a comment
			continue;
		}

		int argc = count_words(input);
		struct Command *command = parse_command(argc, parse_words(input, argc));


		exitStatus = 0;
	} while (exitStatus < 0);


	free(input);	
}

struct Command* parse_command(int argc, char** argv){
	struct Command *command = (struct Command*)malloc(sizeof(command));
	
	command->command = argv[0];
	command->useInputFile = 0;
	command->useOutputFile = 0;
	command->execInBackground = 0;

	command->argc = 1;
	command->argv = (char**)malloc(sizeof(char*));
	command->argv[0] = argv[0];
	
	int i = 1;
	for( ; i < argc; i++){
		if(strcmp(argv[i], "<") == 0){
			command->useInputFile = 1;
			i++;
			command->inputFile = argv[i];

		} else if(strcmp(argv[i], ">") == 0){
			command->useOutputFile = 1;
			i++;
			command->outputFile = argv[i];

		} else if(strcmp(argv[i], "&") == 0){
			command->execInBackground = 1;
		} else {
			command->argc++;
			command->argv = (char**)realloc(command->argv, (command->argc)*sizeof(char*));
			command->argv[command->argc-1] = argv[i];		
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
void expandPIDVar(char* command, int PID){
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
