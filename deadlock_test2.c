#include "types.h"
#include "stat.h"
#include "user.h"

Lock My_Lock;

void function1(void* arg1,void* arg2){
    REQUEST(1);
    sleep(5);
    REQUEST(2);
    RELEASE(2);
    RELEASE(1);
    int* X=(int*)arg2;
    Lock_Acquire(&My_Lock);
    printf(2,"Thread %d Finished with value =%d\n",(*X),2*(*X)+1);
    Lock_Release(&My_Lock);
    exit();
}

void function2(void* arg1,void* arg2){
    REQUEST(2);
    sleep(5);
    REQUEST(1);
    RELEASE(1);
    RELEASE(2);
    int* X=(int*)arg2;
    Lock_Acquire(&My_Lock);
    printf(2,"Thread %d Finished with value =%d\n",(*X),2*(*X)+1);
    Lock_Release(&My_Lock);
    exit();
}

int main(int argc, char *argv[])
{
    int l=3;
    int* size=&l;
    int list[3];
    Lock_Init(&My_Lock);
    thread_create(&function1, (void *)size, (void *)&list[0]);
    thread_create(&function2, (void *)size, (void *)&list[1]);
    for(int i=1;i<=2;i++){ 
        join(i);
    }
    exit();
}
