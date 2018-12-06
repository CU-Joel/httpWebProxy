

#include <stdio.h>
#include <string.h>    //strlen
#include <stdlib.h>    //strlen
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h> //get file age
#include <netdb.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <pthread.h> //for threading , link with lpthread
#include <time.h> //clock_t
#include <signal.h>


#define BUFSIZE 3000

void *connection_handler(void *);
unsigned long hash(unsigned char *str); // for hashing file names
int cacheTime = 0, hostPort=0, hostNameCount=0;
char *hostNames[100];
struct addrinfo *addresses[100];

int main(int argc , char *argv[])
{
    char *p, *q;
    hostPort = (int)strtol(argv[1], &p, 10);
    cacheTime = (int)strtol(argv[2], &q, 10);
    //strncpy(hostPort, argv[1], strlen(argv[1]));
    printf("host port is %d\nchache time is: %d \n", hostPort, cacheTime);
    int sk1, client_sock, c, *new_sock, thread_count=0;
    struct sockaddr_in server , client;
    //pid_t pid = getpid();
    
    //Create socket
    sk1 = socket(AF_INET , SOCK_STREAM  , 0);
    if (sk1 == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");

    
    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(hostPort);
    
    //Bind
    if( bind(sk1,(struct sockaddr *)&server , sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    puts("bind done");
    
    //Listen
    listen(sk1 , 3);
    
    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    
    while(1)
    {
        
    	client_sock = accept(sk1, (struct sockaddr *)&client, (socklen_t*)&c);
        //puts("Connection accepted");
        
        pthread_t sniffer_thread;
        new_sock = malloc(1);
        *new_sock = client_sock;
        
        if( pthread_create( &sniffer_thread , NULL ,  connection_handler , (void*) new_sock) < 0)
        {
            perror("could not create thread");
            return 1;
        }else{
            thread_count++;
            //printf("The thread count is at %d\n", thread_count);
        }
        
        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( sniffer_thread , NULL);
        //puts("Handler assigned");
        
    }
    
    return 0;
}

/*
 * This will handle connection for each client
 */
void *connection_handler(void *sk1)
{
    
    
    int client_sock = *(int*)sk1;
    int read_size, write_size, n, start_index=0, end_index=0;
    char receive_msg[BUFSIZE], send_msg[BUFSIZE],  readbuf[BUFSIZE], file_path[100], path[100];
    int foundHost=0;
    
    //Receive a message from client
    if((read_size = recv(client_sock , receive_msg , BUFSIZE , 0))>=0){
        
        
        if(strncmp(receive_msg, "GET",3)!=0){
            FILE* fp;
            fp = fopen("error.txt", "r");
            while(1){
                bzero(readbuf, BUFSIZE);
                if(fp!=NULL){
                    n = fread(readbuf, sizeof(readbuf), 1, fp);
                    
                    //Send the error message to client
                    write(client_sock , readbuf , sizeof(readbuf));
                    
                    if(feof(fp))
                        break;
                }
            }
            fclose(fp);
            puts("Wasn't a GET");
            free(sk1);
            close(client_sock);
            return 0;
        }
     
        strncpy(send_msg, receive_msg, sizeof(send_msg));
        printf("%s\n", receive_msg);
        bzero(file_path, sizeof(file_path));
        bzero(path, sizeof(path));
        
        //parse http request for file path
        
        for(int i=0; i<BUFSIZE; i++){
            if(receive_msg[i]==' '){
                start_index = i+1;
                
                break;
            }
        }
        for(int j=start_index; j<BUFSIZE; j++){
            if(receive_msg[j]==' '){
                end_index = j;
                break;
            }
        }
        if(start_index && end_index){
            for(int k=start_index; k<end_index; k++){
                path[k-start_index] = receive_msg[k];
            }
        }else{
            puts("Didn't get path");
            return 0;
        }
        
        snprintf(file_path, 21, "%lx", hash((unsigned char *)path));
        printf("\nThis was parsed out as the path: %s size: %lu\n", path, strlen(path));
        
        
        //This is to determine if the chached file has timed out
        struct stat attr;
        stat(file_path, &attr);
        time_t rawtime;
        if(difftime(time(&rawtime), attr.st_mtime) > cacheTime){
            puts("Timeout!");
            remove(file_path);
        }
        
        //open the file
        FILE* fp;
        fp = fopen(file_path, "r");
        if ( fp == NULL ) {
            
            //create file for caching
            fp = fopen(file_path, "w+");
            
            // file was not in cash and now the program will look for the website
            
            
            printf("File not in cache\n\n");
            int status, sk2=0;
            struct addrinfo hints, *servinfo, *p;
            char host[BUFSIZE] ;
            
            start_index=0;
            end_index=0;
            
            for(int i=0; i<BUFSIZE; i++){
                if(receive_msg[i]=='/' && receive_msg[i+1]=='/'){
                    start_index = i+2;
                }
            }
            for(int j=start_index+2; j<BUFSIZE; j++){
                if(receive_msg[j]=='/' || receive_msg[j]==' '){
                    end_index = j;
                    break;
                }
            }
            if(start_index && end_index){
                for(int k=start_index; k<end_index; k++){
                    host[k-start_index] = receive_msg[k];
                }
            }else{
                puts("Didn't get hostname");
                return 0;
            }
            
            //see if hostname and address is in cache
            for(int i=0; i<100; i++){
                if(hostNames[i] == NULL)
                    break;
                if(!strcmp(host, hostNames[i])){
                    foundHost = 1;
                    servinfo = addresses[i];
                    puts("Host name was in cache");
                    break;
                }
            }
            
            if(!foundHost){
                printf("This is the hostname: %s size of hostname is %d\n", host, (int)strlen(host));
                if((status = getaddrinfo(host, "http" , &hints, &servinfo)) != 0) {
                    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
                    //exit(1);
                }else{
                    puts("Got the address info, added it to address list");
                    hostNames[hostNameCount] = malloc(sizeof(host));
                    strncpy(hostNames[hostNameCount], host, strlen(host));
                    addresses[hostNameCount] = servinfo;
                    hostNameCount++;
                    if(hostNameCount==100)
                        hostNameCount = 0; // Only store the 100 most recent
                }
            }
            
            
            for(p = servinfo; p != NULL; p = p->ai_next) {
                //create socket
                sk2 = socket(AF_INET, SOCK_STREAM, 0);
                if(sk2 <0){
                    //puts("Could not create socket");
                }else{
                    //puts("Sk2 created");
                }
                if(connect(sk2, p->ai_addr, p->ai_addrlen)==0){
                    //printf("Connected to IP address: %d\n", (int)p->ai_addr);
                    break;
                }else{
                    close(sk2);
                    //printf("Connection failed on %d\n", (int)p->ai_addr);
                }
            }
            if(sk2){//
                struct timeval timeout;
                timeout.tv_sec = 2;
                timeout.tv_usec = 0;
                setsockopt(sk2, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                int option_value = 1;
                setsockopt(client_sock, SOL_SOCKET, SO_NOSIGPIPE, &option_value, sizeof(option_value));// stop the pesky sig pipe
                setsockopt(sk2, SOL_SOCKET, SO_NOSIGPIPE, &option_value, sizeof(option_value));// stop the pesky sig pipe
                write_size = write(sk2, send_msg, read_size);
                if(write_size < 0)
                    puts("http request forwarding failed");
                if(write_size)
                    //printf("\nThis is being sent to the web server\n%s\n", send_msg);
                while(1){ // loop continuously until break
                    bzero(receive_msg, sizeof(receive_msg));
                    read_size = 0;
                    read_size = recv(sk2 , receive_msg , BUFSIZE , 0);
                    if (read_size)
                        //printf("\nThis was received from the web server\n%s\n", receive_msg);
                    
                    // send response back to client and write to cache file
                    write_size = write(client_sock, receive_msg, read_size);
                    fwrite(receive_msg, read_size, 1, fp);
                    printf("%d bytes written\n", write_size);
                    if(read_size <= 0 || write_size <= 0){
                        close(sk2);
                        break;
                    }
                }
            }
            fclose(fp);
        }else{
            
            puts("Sending from cache.");
            while(1){
                bzero(readbuf, BUFSIZE);
                n = fread(readbuf, sizeof(readbuf), 1, fp);
                
                //Send the file to client
                write(client_sock , readbuf , sizeof(readbuf));
                
                if(feof(fp))
                    break;
            }
            fclose(fp);
        }
        //read_size=0;
    }
    close(client_sock);
    
    //puts("This thread closing");
    if(read_size == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if(read_size == -1)
    {
        perror("recv failed");
    }
    
    //Free the socket pointer
    free(sk1);
    
    return 0;
}


unsigned long hash(unsigned char *str)
{
    
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    
    return hash;
}
