#include "types.h"
#include "stat.h"
#include "user.h"

Lock My_Lock;

void function(void* arg1,void* arg2){
    // requestresource(1);
    // sleep(0.2);
    // releaseresource(1);
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
    for(int i = 0; i < 3; i++){
        thread_create(&function, (void *)size, (void *)&list[i]);
    }
    for(int i=1;i<=3;i++){ 
        join(i);
    }
    exit();
}
