#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
/*
父进程向子进程发送一个字节 ， 子进程打印<pid> :received ping
然后将字节通过管道写回父进程并退出
*/
int main(int argc ,char* argv[]){
    int pipe1[2] , pipe2[2];
    int pid;
    pipe(pipe1);
    pipe(pipe2);
    char buf[] = {'a'};

    int ret = fork();
    /*
    父进程在管道 pipe1[1]发送， 子进程在pipe1[0]读取
    子进程在管道 pipe2[1]发送 ，父进程在pipe2[0]读取
    */
    if(ret == 0){ //子进程
        //子进程要读
        pid = getpid();
        close(pipe2[0]);
        close(pipe1[1]);
        read(pipe1[0] ,buf,1);
        printf("%d: received ping\n",pid);
        write(pipe2[1] ,buf,1);
        exit(0);
    }
    else{//父进程
        pid = getpid();
        close(pipe2[1]);
        close(pipe1[0]);
        write(pipe1[1], buf, 1);
        read(pipe2[0] , buf , 1);
        
        printf("%d: received pong\n", pid);
        exit(0);
    }

}