/**==================================================================================
 *     	    Author:	Harsh Savla, Purshottam Vishwakarma.
 *     Description:	This file contains the code for the tracerouteserver for P538
 *			project 1.
 *===================================================================================**/

#include<iostream>
#include<sstream>
#include<cstdlib>
#include<stdio.h>
#include<string.h>
#include<getopt.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<errno.h>
#include<sys/wait.h>
#include<time.h>
#include<sys/time.h>
#include<signal.h>
#include<vector>
#include<algorithm>
#include<ctime>
#include<fstream>

using namespace std;

/* receive buffer size, i.e. amount data that can be received from the client */
#define BUFFER_SIZE 500

/* threads related variables */
pthread_t threads[100];
//pthread_mutex_t thread_count_mutex;
pthread_mutex_t user_count_mutex;
pthread_mutex_t log_file_mutex;
int thread_count = 0;
int user_count = 0;

/* output log file for administration */
ofstream logFile;

int backlog = 50;	// parameter to listen
int timeout_interval = 30;	// user inactivity 30 secs

/* variable duplicates the stdout file descriptor in main function */
int stdout_backup;
int stderr_backup;

struct rate {

	long num_requests;	
	long num_seconds;
};

struct commandLineArgs {

	long port;
	long max_users;
	long strict_dest;
	rate r;
};

struct container {
	struct sockaddr_storage s_addr;
	int fd;
};

/* log messages to be registered in log file */
const char LOG_MESSAGES[][50] = {
	"[TRACEROUTE REQUEST]: ",
	"[HELP REQUEST]: ",
	"[INVALID COMMAND]: ",
	"[STRICT_DEST ENABLED]: ",
	"[SERVER STARTED]: ",
	"[INVALID DESTINATION]: ",
	"[CONNECTION REQUEST]: ",
	"[CONNECTION REFUSED]: ",
	"[CONNECTION CLOSED]: ",
	"[STRICT DEST ENABLED]: ",
	"[TIMEOUT]: ",
	"[RATE LIMIT EXCEEDED]: "
};

char separator[] =  "---------------------------------------------------------------------------------------------------";

/*this structure stores the default arguments for port, rate, max_users and strict_dest*/
commandLineArgs args = {1216,2,0,{4,60}};
char* program_name;

/* displays the usage guide on the server side */
void print_usage(FILE* stream, int exit_code);

/** Parses the command line arguments */
void fillOptions(int argc, char* argv[])
{	
	int next_option;
	static const char *shortOptions = "p:m:s:r:h?";
	int long_index;
	char* end_ptr;
	long option_argument_value;
	int base_of_args = 10;		// required for string to decimal conversion in strtol
	const char* delimiter = " ";

	static struct option longOptions[] =
	{
		{"PORT",	required_argument,	NULL,	'p'},
		{"port",	required_argument,	NULL,	'p'},
		{"MAX_USERS",	required_argument,	NULL,	'm'},
		{"max_users",	required_argument,	NULL,	'm'},
		{"STRICT_DEST",	required_argument,	NULL,	's'},
		{"strict_dest",	required_argument,	NULL,	's'},
		{"RATE",	required_argument,	NULL,	'r'},
		{"rate",	required_argument,	NULL,	'r'},
		{"help",	no_argument,		NULL,	'h'},
		{NULL, 0, 0, 0}
	};

	do {
		/* getopt_long system call parses the command line arguments */
		next_option = getopt_long(argc, argv, shortOptions,longOptions, NULL);

		long m,s,sec;

		switch(next_option) {

			case 'h':	
				print_usage(stdout, 0);	
				break;			

			case 'm':
				m = strtol(optarg,&end_ptr, base_of_args);	
				if(m > 0)
					args.max_users = m;
				break;

			case 's':
				s =  strtol(optarg,&end_ptr,base_of_args);
				if(s > 0)
					args.strict_dest = s;
				break;

			case -1:
				break;

			case 'r':
				char *tokenizer;
				tokenizer = strtok(optarg,delimiter);
				args.r.num_requests = strtol(tokenizer,&end_ptr, base_of_args);
				tokenizer = strtok(NULL,delimiter);
				if(tokenizer!=NULL)
				{	
					sec =  strtol(tokenizer,&end_ptr, base_of_args);
					if(sec > 0)
						args.r.num_seconds = sec;
				}
				else
					cout << "'RATE' should be entered in quotes for eg --RATE \"5 60\" or -r \"5 60\"" << endl;
				break;

			case 'p':
				args.port = strtol(optarg,&end_ptr,base_of_args);
				break;

			case '?':
				print_usage(stderr, 1);
				break;
		}
	}while(next_option!=-1);

	/*cout << "The values for the options are as follows:" << endl;
	  cout << "port: " << args.port << ", max_users: " << args.max_users << ", strict_dest: " << args.strict_dest << ", rate: " << args.r.num_requests << 
	  " " << args.r.num_seconds << endl;*/
}

void print_usage(FILE* stream, int exit_code)
{
	fprintf(stream, "\n");
	fprintf(stream, "\t\tUsage:\t %s  [option1,option2....optionN] ", program_name);
	fprintf(
			stream,
			"where options can be:\n"
			"\t\t--rate or -r <num_requests> <num_seconds>,\n"
			"\t\t--port or -p  <port_number>,\n"
			"\t\t--max_users or -m <number_of_max_users>,\n"
			"\t\t--strict_dest or -s <strict_dest>"
	       );
	fprintf(stream, "\n\n");
	exit(exit_code);
}

/* returns the sockaddr */
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool isValidCommand(char command[]);
int execCommand(char command[], char dest[], int new_fd);

int codes[] = {

	0,		// close connection with client
	1,		// don't close connection
};

void getTime(char time_buffer[])
{
	time_t rawtime;
	struct tm * timeinfo;

	time (&rawtime);
	timeinfo = localtime ( &rawtime );
	memset(time_buffer, '0', sizeof(time_buffer));
	strftime (time_buffer,80,"%m/%d/%y %H:%M:%S" ,timeinfo);
}

/* thread handler */
void* request_handler(void* arg)
{
	char receiveBuffer[BUFFER_SIZE];
	char client[INET6_ADDRSTRLEN];
	container c = *((container*)arg);
	struct sockaddr client_addr;
	int flags = 0;
	int retBufferSize;
	int new_fd = c.fd;

	char time_buffer[80];
	getTime(time_buffer);
	inet_ntop((c.s_addr).ss_family,get_in_addr((struct sockaddr*)(&(c.s_addr))),client,INET6_ADDRSTRLEN);

	dprintf(stdout_backup,"Got connection from %s at %s\n",client,time_buffer);

	pthread_mutex_lock(&log_file_mutex);
	logFile.open("logfile.txt",ios::app);
	logFile << time_buffer << "\t" << LOG_MESSAGES[6] << "Got connection from " << client << endl;
	logFile.close();
	pthread_mutex_unlock(&log_file_mutex);

	struct timeval timeout;
	timeout.tv_sec = timeout_interval;
	timeout.tv_usec = 0;

	if(setsockopt(new_fd, SOL_SOCKET,SO_RCVTIMEO,&timeout, sizeof(timeout))< 0)
	{
		perror("in thread, timeout");
	}

	if(setsockopt(new_fd, SOL_SOCKET,SO_SNDTIMEO,&timeout, sizeof(timeout))< 0)
	{
		perror("in thread, timeout");
	}

	struct timeval start, end;
	int request_id = 0;
	bool rate_exceeded = false;

	while(true)
	{
		if((retBufferSize = recv(new_fd,(void*)receiveBuffer,BUFFER_SIZE-1,flags))< 0)
		{
			/* checks for timeout */
			if(errno == EWOULDBLOCK && !rate_exceeded)
			{	
				dprintf(new_fd, "Inactivity timeout!");
				if(close(new_fd)!=0)
				{
					perror("Can't close connection");							
				}				

				pthread_mutex_lock(&user_count_mutex);                                                                                  	
				user_count--;           
				dprintf(stdout_backup, "Number of users logged onto the server: %d\n", user_count);	
				pthread_mutex_unlock(&user_count_mutex);
				pthread_mutex_lock(&log_file_mutex);	
				logFile.open("logfile.txt",ios::app);
				logFile << time_buffer << "\t" << LOG_MESSAGES[10] << client << " disconnected " << endl;
				logFile.close();					
				pthread_mutex_unlock(&log_file_mutex);
				dprintf(stdout_backup, "%s disconnected due to timeout\n", client);
				pthread_exit(NULL);			
			}

		}
		else if(retBufferSize >= 2)
		{
	
			/* check for rate limiting */
			request_id++;

			if(request_id == 1)
			{
				rate_exceeded= false;
				gettimeofday(&start, NULL);
			}

			gettimeofday(&end, NULL);

			if(end.tv_sec - start.tv_sec <= args.r.num_seconds)
			{
				if(request_id > args.r.num_requests)
				{
					rate_exceeded = true;
					dprintf(new_fd, "Command not executed. Rate limit exceeded. Sorry, try after approximately %d seconds\n",args.r.num_seconds-(end.tv_sec-start.tv_sec));
					int sleep_time;
					if((sleep_time = args.r.num_seconds - (end.tv_sec - start.tv_sec)) > 0)
					{
						pthread_mutex_lock(&log_file_mutex);
						logFile.open("logfile.txt",ios::app);
						logFile << time_buffer << "\t" << LOG_MESSAGES[11] << client << " violated rate limiting " << endl;
						logFile.close();
						pthread_mutex_unlock(&log_file_mutex);
						sleep(sleep_time);
						request_id = 0;
						rate_exceeded = false;
						memset(receiveBuffer, 0, sizeof(receiveBuffer));
						continue;
					}
				}		
			}			
			else if(end.tv_sec - start.tv_sec >= args.r.num_seconds)
			{
				rate_exceeded= false;
				request_id = 0;
			}

			// for telnet clients
			receiveBuffer[retBufferSize-2] = '\0';

			// for user client
			//receiveBuffer[retBufferSize] = '\0';
			//dprintf(stdout_backup, "In thread, receiveBuffer:%s receiveBuffer length: %d\n",receiveBuffer, strlen(receiveBuffer));
			if(execCommand(receiveBuffer, client,  new_fd) == codes[0])
			{
				if(close(new_fd) != 0)
				{
					perror("Can't close connection");
				}
				pthread_mutex_lock(&user_count_mutex);
				user_count--;                                                 
				dprintf(stdout_backup, "Number of users logged onto the server: %d\n", user_count);					
				pthread_mutex_unlock(&user_count_mutex);
				dprintf(stdout_backup, "%s disconnected\n", client);
				pthread_exit(NULL);
			}
		}
	}
}

/* checks whether the destination entered by the user is valid */
bool isValidDestination(char *dest)
{		
	int i;
	int len = strlen(dest);
	bool valid = true;

	for(i = 0; i < len; i++)
	{
		if((!isalnum(dest[i])) && (dest[i]!= '.') && (dest[i]!='-'))
		{
			valid = false;
			break;
		}	
	}

	return valid;
}

/* prints the usage guide for the client */
void print_help_guide(int new_fd)
{
	dprintf(new_fd, separator);
	dprintf(new_fd, "\n");
	dprintf(new_fd,"\tThe following commands are supported by this server:\n\n");
	dprintf(new_fd, "\ttraceroute [destination machine]:\tPerforms traceroute for the destination machine.\n");
	dprintf(new_fd,"\ttraceroute [file name]:\t\t\tLoops through the file and displays traceroute results for each traceroute command in file.\n");
	dprintf(new_fd,"\ttraceroute me:\t\t\t\tPerforms traceroute using your hostname as target.\n");
	dprintf(new_fd, "\thelp:\t\t\t\t\tDisplay help options.\n");
	dprintf(new_fd, "\tquit:\t\t\t\t\tWill close the session.\n");
	dprintf(new_fd, separator);
	dprintf(new_fd,"\n");
}

/* executes the command issued by the client */
int execCommand(char command[], char dest_addr[], int new_fd)
{
	int commandLength = strlen(command);
	char* token1 = strtok(command," ");
	//cout << token1 << endl;
	char* token2 = strtok(NULL," ");
	//cout << token2 << endl;
	char buffer[BUFFER_SIZE-1];
	bool valid_command = false;
	bool valid_dest = false;
	bool is_traceroute_me = false; // check if command issued is traceroute me

	if(token1!=NULL)
	{
		if(strcmp("traceroute",token1) == 0)
		{
			valid_command = true;

			if(token2!=NULL)
			{
				if(isValidDestination(token2))
				{
					valid_dest = true;
					strcpy(buffer, "traceroute ");
					strcat(buffer, token2);

					if(strcmp("traceroute me", buffer)== 0)
					{
						strcpy(buffer, "traceroute ");
						strcat(buffer, dest_addr);
						is_traceroute_me = true;
					}

					if(strcmp(dest_addr, token2) == 0)
					{
						is_traceroute_me = true;
					}

					if((dup2(new_fd,STDOUT_FILENO)>=0) && (dup2(new_fd, STDERR_FILENO)>= 0))
					{
						char time_buffer[80];
						getTime(time_buffer);

						/* checks if file is present on the sever local dir */
						if(!access(token2, F_OK))
						{
							string str;
							ifstream ifs(token2);

							if(ifs.is_open())
							{
								pthread_mutex_lock(&log_file_mutex);
								logFile.open("logfile.txt",ios::app);
								bool first = true;

								while(ifs.good())
								{
									getline(ifs,str);
									if(str.length() > 11)
									{
										const char* ch = str.c_str();
										cout << endl << "Tracing......" << endl;
										cout << separator << endl;
										system(ch);
										//cout << separator << endl;
										if(first)								
										{
											logFile << time_buffer << "\t" << LOG_MESSAGES[0] << dest_addr << " issued " << buffer << endl;
											first = false;
										}
									}
								}

								logFile.close();                                                                        
								pthread_mutex_unlock(&log_file_mutex);
							}
						} 
						else 
						{	if(((int)(args.strict_dest)) == 0 || (((int)(args.strict_dest==1)) && (is_traceroute_me)))					
							{
								cout << endl << "Tracing......" << endl;
								cout << separator << endl;
								system(buffer);
								cout << separator << endl;
								pthread_mutex_lock(&log_file_mutex);
								logFile.open("logfile.txt",ios::app);
								logFile << time_buffer << "\t" << LOG_MESSAGES[0] << dest_addr << " issued " << buffer << endl;
								logFile.close();
								pthread_mutex_unlock(&log_file_mutex);
							}
							else
							{
								dprintf(new_fd, "Sorry, the 'strict_dest' flag has been enabled.\nYou can only traceroute your own IP address");
								dprintf(new_fd, "\nFor eg. traceroute me\n");	
								pthread_mutex_lock(&log_file_mutex);
								logFile.open("logfile.txt",ios::app);
								logFile << time_buffer << "\t" << LOG_MESSAGES[9] << dest_addr << " issued " << buffer <<", request refused" <<endl;
								logFile.close();
								pthread_mutex_unlock(&log_file_mutex);								
							}
						}
						dup2(stdout_backup, STDOUT_FILENO);
						dup2(stderr_backup, STDERR_FILENO);
					}
				}
			}
		}

		else if(strcmp("help",token1)== 0)
		{
			print_help_guide(new_fd);
			return codes[1];
		}

		else if(strcmp("quit", token1) == 0)
		{
			return codes[0];
		}
	}

	if(valid_command && (!valid_dest))                                                                                    		
	{
		char time_buffer[80];
		getTime(time_buffer);
		pthread_mutex_lock(&log_file_mutex);
		logFile.open("logfile.txt",ios::app);
		logFile << time_buffer << "\t" << LOG_MESSAGES[5] << dest_addr << " issued " << buffer << endl;
		logFile.close();
		pthread_mutex_unlock(&log_file_mutex);
		dprintf(new_fd, "Invalid dest: dest should only consist of letters, numbers and the characters '.' and '-'\n");
		return codes[1];
	}

	if(!valid_command)
	{
		char time_buffer[80];
		getTime(time_buffer);
		pthread_mutex_lock(&log_file_mutex);
		logFile.open("logfile.txt",ios::app);
		logFile << time_buffer << "\t" << LOG_MESSAGES[2] << dest_addr << " issued an invalid command" << endl;
		logFile.close();
		pthread_mutex_unlock(&log_file_mutex);

		dprintf(new_fd,"The command you entered is not valid. Try help for more information\n");
		dprintf(new_fd, separator);
		dprintf(new_fd,"\n");
	}

	return codes[1];
}

int handleConnection()
{
	int sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage client_addr; //  address information
	socklen_t sin_size;
	int yes=1;
	int retval;
	char client[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	char PORT[10];
	sprintf(PORT,"%ld",args.port);

	if((retval=getaddrinfo(NULL,PORT,&hints,&servinfo))!=0)
	{	
		fprintf(stderr, "getaddrinfo error: %s",gai_strerror(retval));
		return 1;
	}

	for(p = servinfo; p!=NULL; p = p->ai_next)
	{
		if((sockfd = socket(p->ai_family,p->ai_socktype,p->ai_protocol))== -1)
		{
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(int)) == -1) 
		{
			perror("setsockopt");
			exit(1);
		}

		if(bind(sockfd, p->ai_addr,p->ai_addrlen)== -1)
		{
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if(p == NULL)	
	{
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo);

	if((listen(sockfd,backlog)) == -1)
	{
		perror("listen");
		exit(1);
	}

	char time_buffer[80];
	getTime(time_buffer);
	logFile.open("logfile.txt",ios::app);
	logFile << time_buffer << "\t" << LOG_MESSAGES[4] << "Server started accepting connections " << endl;
	logFile.close();	

	cout << endl << LOG_MESSAGES[4] << "Server waiting for connections...\n";

	while(true)
	{
		sin_size = sizeof client_addr;
		new_fd = accept(sockfd,(struct sockaddr*)&client_addr ,&sin_size);

		if(new_fd == -1)
		{
			perror("accept");
			continue;
		}

		pthread_mutex_lock(&user_count_mutex);

		if(user_count > ((int)(args.max_users)-1) )
		{
			dprintf(new_fd,"Sorry! the server is busy at this time. Please try again after some time.");
			close(new_fd);

			inet_ntop(client_addr.ss_family,get_in_addr((struct sockaddr*)(&client_addr)),client,INET6_ADDRSTRLEN);
			getTime(time_buffer);

			pthread_mutex_lock(&log_file_mutex);
			logFile.open("logfile.txt", ios::app);
			logFile << time_buffer << "\t" << LOG_MESSAGES[7] << " Connection was refused to " << client << endl;
			pthread_mutex_unlock(&log_file_mutex);
			dprintf(stdout_backup,"%s %s was refused connection\n",LOG_MESSAGES[7],client);
			pthread_mutex_unlock(&user_count_mutex);

			continue;
		}

		cout << "Number of users logged on to the server: " << user_count + 1 << endl;

		user_count++;

		pthread_mutex_unlock(&user_count_mutex);

		container c; 
		c.fd = new_fd;
		c.s_addr =  client_addr;

		pthread_create(&threads[thread_count], NULL, request_handler, &c);
		pthread_detach(threads[thread_count]);		// let thread do its own work and clean up after work is done
		thread_count++;
	}	
	close(sockfd);
}

int main(int argc, char* argv[])
{
	program_name = argv[0];
	stdout_backup = dup(STDOUT_FILENO);
	stderr_backup = dup(STDERR_FILENO);
	fillOptions(argc, argv);
	pthread_mutex_init(&user_count_mutex, NULL);
	handleConnection();
}
