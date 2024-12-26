#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
/*
函数的主要目的是从给定的路径字符串中提取文件或目录的名称
并将其标准化为固定长度（DIRSIZ）的字符串。如果名称长度不足 DIRSIZ，则用空格填充。
这在目录列表显示（如 ls 命令）中非常有用，确保所有名称在输出中对齐。
*/

char*
fmtname(char *path)  //标准化路径
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), 0, DIRSIZ-strlen(p));
  return buf;
}

/*
   1、 当你 find sleep  -> 就会查找当前目录的sleep文件 也就是 ./sleep

*/
void
find(char *path , char * target)//当前的文件夹
{

  //printf("path : %s , target : %s\n",path , target);
  char buf[512], *p;
  int fd;
 struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){ //尝试打开文件夹
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){ 
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

//   if(!strcmp(fmtname(path) , target)){ //它们相等说明是注释上的第一点
//     printf("%s\n" , target);
//     return;
//   }

  switch(st.type){ //对path考察
  case T_FILE: 
    break;

  case T_DIR: //如果是文件夹
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0 || !strcmp(de.name , ".") || !strcmp(de.name , ".."))//必须使用双引号
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      

      if(!strcmp(fmtname(buf) , target)){ //标准化只会保留后面的参数
        printf("%s\n" , buf);
      }
        find(buf , target);
      
        

    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
    if(argc == 1){
        fprintf(2 , "error!\n");
        exit(1);
    }
    if(argc == 2){ // find console
        find("." , argv[1]);
        exit(0);
    }
    if(argc == 3){
        find(argv[1] , argv[2]);
        exit(0);
    }

  exit(0);
}
