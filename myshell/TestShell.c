#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{
    FILE *fp;

    int num = 20;
    while (num != 0)
    {
        sleep(2);
        num--;
        if ((fp = fopen("/root/Code/CSAPP/myshell/result.txt", "at+")) == NULL)
        {
            printf("Cannot open file, press any key to exit!\n");
            exit(1);
        }
        char str[102] = {0}, strTemp[100] = "Hello";
        strcat(str, strTemp);
        strcat(str, "\n");
        fputs(str, fp);
        fclose(fp);
    }

    return 0;
}