#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "lsbevent_parse.h"

int main (int argc, char ** argv)
{
    if (argc < 2) {
        printf("Nothing to process, will exit.\n");
        return -1;
    }
    else {
        printf("Processing lsb.stream record => %s\n", argv[1]);
        char* res = readlsbStream(argv[1]);
        if (res == NULL) {
        	printf("Parsing error\n");
        	return 1;
        }
        else {
        	printf("Result => %s\n", res );
        	return 0;
        }
    }
}