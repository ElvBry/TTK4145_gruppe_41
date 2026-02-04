#include <stdio.h>
#include <unistd.h>   // sleep()
#include <stdlib.h>   // exit()

int main() {
    printf("Program A started\n");
    fflush(stdout);

    for (int i = 5; i > 0; i--) {
        printf("Running... %d\n", i);
        fflush(stdout);
        sleep(1);
    }

    printf("Program A exiting (simulated crash)\n");
    fflush(stdout);

    return 0;   // program terminates
}
