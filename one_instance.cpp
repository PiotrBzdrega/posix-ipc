#include <sys/file.h>
#include <errno.h>
#include <string.h> //strerror
#include <mqueue.h> //mq
#include <signal.h> //SIGEV_THREAD

#include <iostream>
#include <chrono>
#include <thread>
// #define _USE_FLOCK_

auto handle_error = [](const char* msg)
{
    perror(msg); 
    exit(EXIT_FAILURE);
};
std::string mq_name="/mq_one_instance";


void new_msg_queue(union sigval sv)
{
    //TODO: change for c++ casting
    mqd_t mqdes = *((mqd_t *) sv.sival_ptr);

    struct mq_attr attr;
    /* Determine max. msg size; allocate buffer to receive msg */
    if (mq_getattr(mqdes, &attr) == -1)
    {
        handle_error("mq_getattr");
    }    
    
    printf("Attributes of MQ %s \n",mq_name.c_str());
    printf("Maximum message size: %ld bytes\n", attr.mq_msgsize);
    printf("Maximum number of messages: %ld\n", attr.mq_maxmsg);

    char *buf;
    buf = (char*)malloc(attr.mq_msgsize); //Maximum message size
    if (buf == NULL)
    {
        handle_error("malloc");
    }
        
    //received byte count
    ssize_t rcv = mq_receive(mqdes, buf, attr.mq_msgsize, NULL);
    if (rcv == -1)
        handle_error("mq_receive");

    printf("Read %zd bytes from MQ %s: %s\n", rcv, mq_name.c_str(), buf);
    free(buf);
    exit(EXIT_SUCCESS);         /* Terminate the process */
}


using namespace std::chrono_literals;
int main(int argc, char *argv[])
{
    mqd_t mqdes;
    int pid_file = open("/tmp/one.pid", O_CREAT | O_RDWR, 0666); //The return value is a file descriptor
    /*The file descriptor is used in subsequent system calls (read(2), write(2), lseek(2), fcntl(2)*/
#ifdef _USE_FLOCK_
    int rc = flock(pid_file, LOCK_EX | LOCK_NB); // Exclusive lock | Don't block when locking
#else
/*The fcntl() system call provides record
locking, allowing processes to place multiple read and write locks on different
regions of the same file.*/

    struct flock fl;

    // Initialize the flock structure
    fl.l_type = F_WRLCK;   // Exclusive lock
    fl.l_whence = SEEK_SET; //Seek from beginning of file
    fl.l_start = 0;
    fl.l_len = 0; //whole file if 0

    // Attempt to acquire the lock
    int rc = fcntl(pid_file, F_SETLK, &fl); //Set record locking info (non-blocking)
#endif
    
    if(rc) 
    {
        printf("Read arguments:\n");
        for (size_t i = 0; i < argc; i++)
        {
            printf("%s\t",argv[i]);
        }
        printf("\n");
        
        // Open the message queue for sending
        mqdes = mq_open(mq_name.c_str(), O_WRONLY);
        if (mqdes == (mqd_t)-1) {
            handle_error("mq_open");
        }

        printf("sizeof(%s) = %ld\n",argv[argc-1], strlen(argv[argc-1]));

        if (mq_send(mqdes, argv[argc-1], strlen(argv[argc-1]), 0) == -1) 
        {
            handle_error("mq_send");
        }

        std::cout<<"errno: "<<strerror(errno)<<"\n";
        if(EWOULDBLOCK == errno)
        {
            std::cout<<"\033[0;91m another instance is running \033[0m\n";
#ifndef _USE_FLOCK_
            int rc = fcntl(pid_file, F_GETLK, &fl); //Get record locking info
            std::cout<<"\033[0;91m Process ID of the process that holds the blocking lock "<<fl.l_pid<<"\033[0m\n";       
#endif
            exit(1);
        }
    }
    else
    {
        mqdes = mq_open(mq_name.c_str(),O_RDONLY | O_CREAT /* | O_EXCL | O_NONBLOCK*/,0600,NULL);
        if (mqdes == (mqd_t) -1)
        {
            handle_error("mq_open");
        }

        struct sigevent sev;

        sev.sigev_notify = SIGEV_THREAD; //Deliver via thread creation
        sev.sigev_notify_function = new_msg_queue; // thread function 
        sev.sigev_notify_attributes = NULL;
        sev.sigev_value.sival_ptr = &mqdes;   /* Arg. to thread func. */

        /* register a notification request for the message queue */
        if (mq_notify(mqdes, &sev) == -1)
        {
            handle_error("mq_notify");
        }

        while (1)
        {
            std::cout<<"only one instance\n";
            std::this_thread::sleep_for(2000ms);
        }
        
    }
    return 0;
}