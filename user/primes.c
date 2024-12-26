#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define MAX 36
#define  NO '0'
#define YES '1'

void prime(int rp , int wp){
    //子进程
    char nums[MAX];
    
    read(rp , nums , MAX);
    int t = 0;
    //素数筛
    for(int i =0;i<MAX;++i){
        if(nums[i] == NO){
            t = i;
            break;
        }
    }

    if(t == 0){
        exit(0);
    }
    printf("prime %d\n", t);
    nums[t] = YES;
    for(int i =t+1;i<MAX;++i){
        if(i % t == 0)nums[i] = YES;
    }

    int pid = fork();
    if(pid == 0){
        prime(rp ,wp);
        exit(0);
    }
    else{
        write(wp , nums , MAX);
        wait(0);
    }


}


int main(int argc , char *argv[]){
    int p[2];
    pipe(p);

    char nums[MAX];
    for(int i =0;i<MAX;++i){
        nums[i] = NO;
    }

    int pid = fork();
    if(pid == 0){
        prime(p[0] , p[1]);
        wait(0);
    }
    else{
        //1和2都是质数
        nums[0] = YES;
        nums[1] = YES;
        close(p[0]);//关闭读
        write(p[1] , nums , MAX);
        wait(0);
        close(p[1]);
    }
   exit(0);
}