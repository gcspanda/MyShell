/**
 * 本项目是由CSAPP shelllab大实验改造而来
 * 在原实验中主要实现以下七个函数
 * 1. void eval(char *cmdline); 相当于本项目的 void System(char *cmdline); 
 * 2. int builtin_cmd(char **argv);
 * 3. void do_bgfg(char **argv);
 * 4. void waitfg(pid_t pid);
 * 5. void sigchld_handler(int sig);
 * 6. void sigtstp_handler(int sig);
 * 7. void sigint_handler(int sig);
 * 
 * 本项目中 对以上七个函数进行实现 并自行实现其余函数 因此部分细节可能与原实验代码不同
 * 由于个人原因 完成这个并不是很轻松 也借鉴很多大佬的代码 不过好在最终基本实现我的预期
 * 坚持完成这个项目的原因有三点：
 * 1. 强化对信号的理解
 * 2. 自己亲自动手完整的设计出一个小项目 (毕竟在原实验中 你不需要关心其他细节的设计)
 * 3. 也算是对自己C语言(指针)的一个练习吧
 * 
 * 本项目最多只能算是个人练手 毕竟还有一些细节没有完善 甚至还有一些bug没发现(可能发现了也不知道咋改)
 * 具体操作见《MyShell操作手册》
 * 
 * --by Panda
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#define MAXLINE 100    // 一行最多字符数
#define MAXARGS 10     // 最多参数个数
#define MAXJOBS 16     // 最大作业数
#define MAXJID 1 << 16 // 最大JID

// 作业的状态
#define UNDEF 0
#define FG 1 // 前台执行
#define BG 2 // 后台执行
#define ST 3 // 停止

// volatile sig_atomic_t flag;

// 作业结构体
struct job_struct
{
    pid_t pid;
    int jid;
    int status;
    char cmdline[MAXLINE];
} joblist[MAXJOBS]; // 定义作业列表

// 全局变量
extern char **environ;       // 环境变量
char prompt[] = "MyShell> "; // 提示符
int nextjid = 1;             // 下一个JID的编码

// 功能函数
void System(char *cmdline);            // 自定义System()函数
int parseline(char *buf, char **argv); // 解析参数
int builtin_cmd(char **argv);          // 执行内置函数
void Exit(char *string, int num);      // 自定义exit()函数
void puts_error(char *errstr);
void Kill(pid_t pid, int sig); //自定义kill()函数

// 信号处理
void sigchld_handler(int sig); // 处理SIGCHLD信号
void sigint_handler(int sig);  // 处理SIGINT信号    Ctrl c
void sigtstp_handler(int sig); // 处理SIGTSTP信号   Ctrl z
void sigquit_handler(int sig); // 处理SIGQUIT       Ctrl / 相当于强化的Ctrl c

// 作业处理
void initjobs(struct job_struct *job);
void listjobs(struct job_struct *job);
void clearjob(struct job_struct *job);
int addjob(struct job_struct *job, pid_t pid, int status, char *cmdline);
int deletejob(struct job_struct *job, pid_t pid);
int getfgjid(struct job_struct *job);                         // 获取前台作业jid
int getfgpid(struct job_struct *job);                         // 获取前台作业的pid
struct job_struct *getjob(struct job_struct *job, pid_t pid); // 根据pid获取job

// 前/后台 相关函数
void do_bgfg(char **argv);
void waitfg(pid_t pid); // 如果判定是前台执行 那么应该使用此函数持续等待回收该进程

int main()
{
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);

    initjobs(joblist);

    char cmdline[MAXLINE];
    printf("%s", prompt);

    while (1)
    {
        fgets(cmdline, MAXLINE, stdin);
        if (feof(stdin))
            exit(0);
        System(cmdline);
        fflush(stdout);
    }
    exit(0);
}

void System(char *cmdline)
{
    pid_t pid;
    int isbg; // 后台运行标志位
    sigset_t mask_all, prev_all;
    sigset_t mask_one;

    char buf[MAXLINE];
    char *argv[MAXLINE];
    strcpy(buf, cmdline);
    isbg = parseline(buf, argv);

    if (argv[0] == NULL) //如果没有参数直接退出
        Exit("NO Command", 0);

    if (!builtin_cmd(argv)) // 只有非内置命令才能继续执行
    {
        sigfillset(&mask_all);
        sigemptyset(&mask_one);
        sigaddset(&mask_one, SIGCHLD);
        signal(SIGCHLD, sigchld_handler);
        signal(SIGTSTP, sigtstp_handler);
        signal(SIGINT, sigint_handler);
        signal(SIGQUIT, sigquit_handler);

        sigprocmask(SIG_BLOCK, &mask_one, &prev_all);

        if ((pid = fork()) < 0)
            Exit("Fork error", 1);
        else if (pid == 0)
        {
            sigprocmask(SIG_UNBLOCK, &mask_one, NULL);
            // 下面这个printf和 sigchld_handler()里面的子进程正常回收打印函数是相对应的 都是测试
            // printf("\n    Tips: child %d fork success\n\n", (int)getpid());
            setpgid(0, 0);
            // 这里要区分执行的是shell命令 还是 二进制可执行程序
            if (argv[0][0] == '.' && argv[0][1] == '/')
            {
                if (execve(argv[0], argv, environ) < 0)
                    Exit(argv[0], 0);
            }
            else
            {
                char *command[MAXARGS];
                command[0] = "sh";
                command[1] = "-c";
                command[2] = cmdline;
                command[3] = NULL;

                if (execve("/bin/sh", command, environ) < 0)
                    Exit(command[2], 0);
            }

            exit(0);
        }
        sigprocmask(SIG_BLOCK, &mask_all, NULL);
        // 这里要注意 至多有一个前台作业 和 0个或多个后台作业
        addjob(joblist, pid, isbg ? BG : FG, cmdline);
        struct job_struct *job = getjob(joblist, pid);

        sigprocmask(SIG_SETMASK, &prev_all, NULL);
        if (isbg)
        {
            printf("[%d] (%d) %d %s\n", job->jid, job->pid, job->status, job->cmdline);
            printf("%s", prompt);
        }
        else
            waitfg(pid);
    }
    return;
}

int parseline(char *buf, char **argv) // 参数解析 并要判断出是否为后台执行
{
    int argc;
    char *delim;
    buf[strlen(buf) - 1] = ' ';
    while (*buf && (*buf == ' '))
        buf++;

    argc = 0;
    while ((delim = strchr(buf, ' ')))
    {
        argv[argc++] = buf;
        *delim = '\0';

        buf = delim + 1;
        while (*buf && (*buf == ' '))
            buf++;
    }
    argv[argc] = NULL;

    if (!strcmp(argv[argc - 1], "&")) // 判断是否为后台执行 是则返回1 否则返回0
    {
        argv[--argc] = NULL; // 最后的 & 不需要带入command
        return 1;
    }

    return 0;
}

int builtin_cmd(char **argv) // 处理内置命令
{
    if (!strcmp(argv[0], "quit")) // 直接退出主程序
        exit(0);

    if (!strcmp(argv[0], "jobs")) // 列出当前job列表
    {
        listjobs(joblist);
        printf("%s", prompt); // 由于jobs属于内置命令 所以要单独处理打印一下提示符
        return 1;
    }

    if (!strcmp(argv[0], "fg") || !strcmp(argv[0], "bg")) // 前台/后台 指令
    {
        do_bgfg(argv);
        return 1;
    }

    return 0;
}

void Exit(char *string, int num)
{
    // fprintf(stderr, "%s Command not found\n", string);
    printf("warnging: %s command not found\n", string);
    exit(num);
}

void puts_error(char *errstr)
{
    fprintf(stderr, "%s command not found\n", errstr);
}
/**
 * 这里要注意 在调用自定义Kill()的时候传入的已经是-pid
 * 所以在执行kill()的时候无需在传入的pid前面再加 “-”
*/
void Kill(pid_t pid, int sig)
{
    if (kill(pid, sig) == -1)
        printf("Kill PID : %d SIG : %d error\n", pid, sig);
}

// 信号处理
void sigchld_handler(int sig) // 处理SIGCHLD信号
{
    pid_t pid;
    int status;
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

        if (WIFEXITED(status)) // 进程正常中止
        {
            // printf("\n    Tips: child %d terminated normally\n\n", pid); // 这个是用来回馈是哪个进程被回收
            deletejob(joblist, pid);
        }
        if (WIFSIGNALED(status)) // 进程被信号被中断 注意中断和挂起的区别 中断是彻底结束 挂起是放到后台
        {
            printf("\n    Tips: child %d terminated by signal %d\n\n", pid, WTERMSIG(status));
            deletejob(joblist, pid);
        }
        if (WIFSTOPPED(status)) // 进程由信号挂起
        {
            printf("\n    Tips: child %d stopped by signal %d\n\n", pid, WSTOPSIG(status));
            struct job_struct *job = getjob(joblist, pid);
            if (job != NULL)
                job->status = ST;
        }

        sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
    printf("%s", prompt); // 信号最后是被这里回收的 所以每当回收完成时都需要打印一下提示符

    if (pid < 0 && errno != ECHILD) // 前面加上pid < 0的判定条件 可以不用打印错误
        puts_error("waitpid error");

    errno = olderrno;
    return;
}

/**
 * SIGINT 接收来自键盘的中断
 * 这个是彻底的中止作业
*/
void sigint_handler(int sig) // 处理SIGINT信号    Ctrl c
{
    pid_t pid = getfgpid(joblist);
    if (pid != 0)
        Kill(-pid, SIGINT);

    return;
}

/**
 * SIGTSTP 接收来自终端的停止信号 Ctrl z
 * 并将前台作业暂停放到后台去 
*/
void sigtstp_handler(int sig) // 处理SIGTSTP信号   Ctrl z
{
    pid_t pid;
    struct job_struct *job;
    int olderrno = errno;
    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);

    sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
    pid = getfgpid(joblist);
    job = getjob(joblist, pid);
    job->status = ST;

    sigprocmask(SIG_SETMASK, &prev_all, NULL);

    if (pid == 0)
    {
        puts_error("Can not found FG PID");
        return;
    }

    Kill(-pid, SIGTSTP);

    errno = olderrno;
    return;
}
void sigquit_handler(int sig) // 处理SIGQUIT  终端退出符
{
    printf("SIGQUIT\n");
    exit(1);
}

// 作业处理
void initjobs(struct job_struct *job)
{
    for (int i = 0; i < MAXJOBS; i++)
        clearjob(&job[i]);
}

void listjobs(struct job_struct *job)
{
    printf("  PID  JID  status  command\n");
    for (int i = 0; i < MAXJOBS; i++)
        printf("%5d%5d%8d  %s\n",
               job[i].pid, job[i].jid, job[i].status, job[i].cmdline);
}

void clearjob(struct job_struct *job)
{
    job->pid = 0;
    job->jid = 0;
    job->status = UNDEF;
    job->cmdline[0] = '\0';
}

int addjob(struct job_struct *job, pid_t pid, int status, char *cmdline)
{
    if (pid < 0)
        return 0;

    for (int i = 0; i < MAXJOBS; i++)
    {
        if (job[i].pid == 0)
        {
            job[i].pid = pid;
            job[i].jid = nextjid++;
            if (nextjid > 16)
                nextjid = 1;

            job[i].status = status;
            strcpy(job[i].cmdline, cmdline);
            return 1;
        }
    }
    puts_error("Joblist full\n");
    return 0;
}

int deletejob(struct job_struct *job, pid_t pid)
{
    if (pid < 1)
    {
        puts_error("Can not delete this job\n");
        return 0;
    }

    for (int i = 0; i < MAXJOBS; i++)
    {
        if (joblist[i].pid == pid)
        {
            clearjob(&joblist[i]);
            return 1;
        }
    }
    puts_error("Can not find this job\n");
    return 0;
}

int getfgjid(struct job_struct *job) // 获取前台作业的JID
{
    for (int i = 0; i < MAXJOBS; i++)
    {
        if (job[i].status == FG)
            return job[i].jid;
    }
    return 0;
}

int getfgpid(struct job_struct *job) // 获取前台作业的PID
{
    for (int i = 0; i < MAXJOBS; i++)
        if (job[i].status == FG)
            return job[i].pid;

    return 0;
}

struct job_struct *getjob(struct job_struct *job, pid_t pid) // 根据pid获取对应的jid
{
    for (int i = 0; i < MAXJOBS; i++)
    {
        if (job[i].pid == pid)
            return &job[i];
    }
    return NULL;
}

/**
 * 关于前/后台执行 这里只做简单的实现
 * fg: 将后台中的命令调到前台来执行 fg %N 指定具体作业 N是JID
 * bg: 将后台暂停的命令继续执行     bg %N 同上
 * Ctrl z 将前台正在执行的命令放到后台 并暂停
*/
void do_bgfg(char **argv)
{
    pid_t pid;
    int jid;
    struct job_struct *job; // 指向本次fg / bg 需要操作的job
    char *jobid = argv[1];  // 注意 这里格式应该是 %N
    if (jobid == NULL)
    {
        printf("Can not found this job : %s\n", jobid);
        return;
    }

    if (jobid[0] == '%' && isdigit(jobid[1]))
    {
        jid = atoi(&jobid[1]);            // 获取JID
        for (int i = 0; i < MAXJOBS; i++) // 获取对应的pid
            if (joblist[i].jid == jid)
            {
                pid = joblist[i].pid;
                job = getjob(joblist, pid);
                break;
            }
    }
    else if (isdigit(jobid[0])) // 如果输入的直接就是pid
    {
        pid = atoi(&jobid[0]);
        job = getjob(joblist, pid);
    }
    else
    {
        printf("fg or bg error\n");
        return;
    }

    if (job == NULL)
    {
        printf("Can not found this job and pid : %d\n", pid);
        return;
    }

    Kill(-pid, SIGCONT); // 无论是fg还是bg 都是让程序继续执行 所以发送SIGCONT信号

    if (!strcmp(argv[0], "fg"))
    {
        job->status = FG;
        waitfg(pid);
    }
    else
    {
        job->status = BG;
        printf("[%d] (%d) %d %s\n", job->jid, job->pid, job->status, job->cmdline);
        printf("%s", prompt); // bg是让后台挂起指令继续执行 所以执行完bg需要打印提示符
    }
    return;
}

void waitfg(pid_t pid) // waitfg 阻塞进程号为pid的进程 直到其不再是前台作业
{
    while ((pid == getfgpid(joblist)))
        sleep(0);

    return;
}