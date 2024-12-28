#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

//find . b | xargs grep hello
/*
拿到 grep hello xxx(路径) ， 然后fork + exec 来调用 grep
1、 拿到路径
参考 ulib.c and sh.c

*/
#define MAXSIZE 512
char*
getss(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    
    if(c == '\n' || c == '\r' || c == ' ')
      break;

    buf[i++] = c; //keng
  }
  buf[i] = '\0';
  return buf;
}

int
getcmdd(char *buf, int nbuf)
{
  fprintf(2, "$ ");
  memset(buf, 0, nbuf);
  getss(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}
int main(int argc , char * argv[]){
    //xargs grep hello
    if(argc < 2){
        fprintf(2 , "Usage xargs command\n");
        exit(1);
    }
    char buf[MAXSIZE];
    int _argc = argc -1;
    char *_argv[MAXARG];

    for(int i =0;i<_argc;++i){
        _argv[i] = argv[i+1];
    }

    // for(int i =0;i<_argc;++i)printf("_argv = %s\n" , _argv[i]);

    // printf("\n");
    // printf("\n");


    while((getcmdd(buf ,MAXSIZE)) >=0){
        if(fork() == 0){
            //printf("_argc = %d\n" , _argc);
            
            _argv[_argc] = buf;
            _argc++;
            
            // printf("_argc = %d\n" , _argc);

            // printf("buf=%s\n" , buf);

            // for(int i =0;i<_argc;++i){
            //     printf("argv = %s\n" , _argv[i]);
            // }
            exec(argv[1] , _argv);
            fprintf(2, "exec %s failed\n", argv[1]); // 执行失败
            exit(0);

        }
        else{
            wait(0);
        }
    }


    exit(0);
}