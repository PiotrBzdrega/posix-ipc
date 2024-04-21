#include <sys/file.h>
#include <errno.h>
#include <string.h> //strerror
#include <mqueue.h> //mq
#include <signal.h> //SIGEV_THREAD
#include <sys/inotify.h> //inotify
#include <limits.h> //NAME_MAX

#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
// #define _USE_FLOCK_

#define BUF_LEN (sizeof(struct inotify_event) + NAME_MAX + 1)

auto handle_error = [](const char* msg)
{
    perror(msg); 
    exit(EXIT_FAILURE);
};
std::string mq_name="/mq_one_instance";

void /* Display information from inotify_event structure */
displayInotifyEvent(struct inotify_event *i)
{
    printf(" wd =%2d; ", i->wd);
    if (i->cookie > 0)
    {
        printf("cookie =%4d; ", i->cookie);
    }

    printf("mask = ");
    if (i->mask & IN_ACCESS) printf("IN_ACCESS ");
    if (i->mask & IN_ATTRIB) printf("IN_ATTRIB ");
    if (i->mask & IN_CLOSE_NOWRITE) printf("IN_CLOSE_NOWRITE ");
    if (i->mask & IN_CLOSE_WRITE) printf("IN_CLOSE_WRITE ");
    if (i->mask & IN_CREATE) printf("IN_CREATE ");
    if (i->mask & IN_DELETE) printf("IN_DELETE ");
    if (i->mask & IN_DELETE_SELF) printf("IN_DELETE_SELF ");
    if (i->mask & IN_IGNORED) printf("IN_IGNORED ");
    if (i->mask & IN_ISDIR) printf("IN_ISDIR ");
    if (i->mask & IN_MODIFY) printf("IN_MODIFY ");
    if (i->mask & IN_MOVE_SELF) printf("IN_MOVE_SELF ");
    if (i->mask & IN_MOVED_FROM) printf("IN_MOVED_FROM ");
    if (i->mask & IN_MOVED_TO) printf("IN_MOVED_TO ");
    if (i->mask & IN_OPEN) printf("IN_OPEN ");
    if (i->mask & IN_Q_OVERFLOW) printf("IN_Q_OVERFLOW ");
    if (i->mask & IN_UNMOUNT) printf("IN_UNMOUNT ");
    printf("\n");

    if (i->len > 0)
    {
        printf(" name = %s\n", i->name);
    }
}

void new_msg_queue(union sigval sv)
{
    //TODO: change for c++ casting
    mqd_t mqdes = *(static_cast<mqd_t*>(sv.sival_ptr));

    struct mq_attr attr;
    /* Determine max. msg size; allocate buffer to receive msg */
    if (mq_getattr(mqdes, &attr) == -1)
    {
        handle_error("mq_getattr");
    }    
    
    // printf("Attributes of MQ %s \n",mq_name.c_str());
    // printf("Maximum message size: %ld bytes\n", attr.mq_msgsize);
    // printf("Maximum number of messages: %ld\n", attr.mq_maxmsg);

    /* Re-register notification , mq_notify is only one-shot event*/

    struct sigevent sev;
    sev.sigev_notify = SIGEV_THREAD; //Deliver via thread creation
    sev.sigev_notify_function = new_msg_queue; // thread function 
    sev.sigev_notify_attributes = NULL;
    sev.sigev_value.sival_ptr = sv.sival_ptr;   /* Arg. to thread func. */
    if (mq_notify(mqdes, &sev) == -1)
    {
        handle_error("mq_notify");
    }

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
    int ret = std::strcmp("exit",buf);
    free(buf);
    if (ret == 0)
    {
        exit(EXIT_SUCCESS);         /* Terminate the process */
    }
    
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
#pragma region MESSAGE QUEUE FOR READING
        mqdes = mq_open(mq_name.c_str(),O_RDONLY | O_CREAT | O_NONBLOCK /* | O_EXCL */,0600,NULL);
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

        /* clean mq in case of previous remains ,notification won't be rised in case of non-empty queue*/
        struct mq_attr attr;
        if (mq_getattr(mqdes, &attr) == -1)
        {
            handle_error("mq_getattr");
        }    

        char *emp_buf;
        emp_buf = (char*)malloc(attr.mq_msgsize); //Maximum message size
        if (emp_buf == NULL)
        {
            handle_error("malloc");
        }
        while(mq_receive(mqdes, emp_buf, attr.mq_msgsize, 0) >= 0)
        {
          std::cout << "empty the mq " << emp_buf << "\n";
        }
#pragma endregion

#pragma region FILE MONITORING INIT
        /* non-blocking file monitoring */
        int inotifydes = inotify_init1(IN_NONBLOCK);
        if(inotifydes == -1)
        {
            handle_error("inotify_init1");
        }
        /* inotify does not track not existing files, that is why i sniff directory .
            when file is modified it trigger many announcements since it creates/delete various swap files */
        int watchdes = inotify_add_watch(inotifydes, "/home/ccpp/one_instance/", IN_ALL_EVENTS);
        if(watchdes == -1)
        {
            handle_error("inotify_add_watch");
        }

        struct inotify_event* event;
        char buf[BUF_LEN];
        ssize_t bytes;
#pragma endregion

        while (1)
        {   
            bytes = read(inotifydes,buf,BUF_LEN);
            if (bytes == 0)
            {
                handle_error("read() from inotify fd returned 0!");
            }
            else
            if(bytes == -1 && EAGAIN != errno)
            {
                std::cout<<"errno: "<<strerror(errno)<<"\n";
                handle_error("read");
            }
            else
            if (bytes > 0)
            {
                printf("Read %ld bytes from inotify fd\n", (long) bytes);
                char * p;

                for (p = buf; p < buf + bytes;)
                {
                    event = (struct inotify_event *) p;
                    displayInotifyEvent(event);

                    p += sizeof(struct inotify_event) + event->len;
                }
            }

            std::cout<<"only one instance\n";
            std::this_thread::sleep_for(2000ms);
        }
        
    }
    return 0;
}