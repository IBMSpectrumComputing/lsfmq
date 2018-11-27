static const char *rcsId(const char *id) {return(rcsId(
"@(#)$Id$"));}

/*
 * Copyright (c) 2005-2007 by Platform Computing Corporation. 
 * All rights reserved.
 * 
 * This software is the confidential and proprietary information 
 * of Platform Computing, Inc. You shall not disclose such Confidential
 * Information and shall use it only in accordance with the terms of the
 * license agreement you entered into with Platform.
 * 
 * Author    sxu
 * Created   Aug 28, 2006
 */

/* 
 * Use to count job number by the given job name.
 *
 * EXPORTED ROUTINES:
 *   int countJobByName(char*, char**);
 *
 * EXPORTED VARIABLES:
 *   NULL
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "lsbatch.h"
#include "job_array.h"

#define INDEX_RANGE_DELIM	","
#define START_JOB_INDEX		1
#define INIT_INDEX_NUMBER	1000
#define INDEX_INCREMENT		100


typedef struct tagIndexRange {
    int lower;
    int upper;
    int step;
} IndexRange;

/*
 * Get max job array size by calling LSF API
 * Returns:
 *   Max job array size.
 */
static int getMaxJobArraySize()
{
    struct parameterInfo *paramInfo;
    int maxJobArraySize = 0;

    /* Initialize lsb evn */
    if (lsb_init(NULL) < 0) {
        return -1;
    }

    /* Get parameters by calling lsb API */
    if (!(paramInfo = lsb_parameterinfo(NULL, NULL, 0))) {
        return -1;
    }

    maxJobArraySize = paramInfo->maxJobArraySize;
    return maxJobArraySize;
}


/*
 * Get comma delimited range list from the given job name.
 * Params:
 *   jobName - IN Specify job name
 * Returns:
 *  String containing comma delimited range list. The pointer need to be released, after it's used.
 */
static char *getIndexRangeListString(char *jobName)
{
    char *startIndex;           /* Position of "[" in job name */
    char *endIndex;             /* Position of "]" in job name */
    int jobNameLen;             /* Length of job name */
    char *rangeList = NULL;     /* Temporary index list of job name */
    int rangeListLen;           /* Length of job index list */

    /* If job name is null, or length of job name is 0, treat it as one job */
    if (jobName == NULL) {
        return NULL;
    }

    /* Get length of job name */
    jobNameLen = strlen(jobName);

    if (jobNameLen == 0) {
        return NULL;
    }

    /* Get the start index of array index */
    startIndex = strchr(jobName, '[');
    if (startIndex == NULL) {
        return NULL;
    }

    /* Get the end index of array index */
    endIndex = strchr(jobName, ']');
    if (endIndex == NULL) {
        return NULL;
    }

    /* Get job list length */
    rangeListLen = (endIndex - startIndex) - 1;

    /* Invalid job array index */
    if (rangeListLen <= 0) {
        return NULL;
    }

    /* Copy index list */
    rangeList = (char *) malloc(rangeListLen + 1);
    if (rangeList == NULL) {
    	return NULL;
    }
    memcpy(rangeList, startIndex + 1, rangeListLen);
    /* Set end sign. */
    rangeList[rangeListLen] = 0x00;

    return rangeList;
}


/*
 * Parase index range string to IndexRangeList node.
 * Params:
 *   indexRangeString - IN Index range string.
 *   maxJobArraySize - IN Max job array size.
 *   indexRange - OUT Address of IndexRange struct.
 * Returns:
 *   0, if succeded, otherwise, -1.
 */
static int
parseIndexRangeString(char *indexRangeString, IndexRange * indexRange)
{
    int rangeLen;
    char *dashIndex = NULL;
    char *tmpIndex = NULL;
    char *colonIndex = NULL;

    indexRange->lower = -1;
    indexRange->upper = -1;

    if (indexRangeString == NULL) {
        return -1;
    }

    /* Get length of range */
    rangeLen = strlen(indexRangeString);

    if (rangeLen == 0) {
        return -1;
    }

    dashIndex = strchr(indexRangeString, '-');
    colonIndex = strchr(indexRangeString, ':');
    
    tmpIndex = (char *) malloc(rangeLen * sizeof(char) + 1);
    if (tmpIndex == NULL) {
        return -1;
    }
    
    if (dashIndex == NULL) {
        /* Non-range */
        if (colonIndex == NULL) {
        	/* Without step */
	        indexRange->lower = atoi(indexRangeString);
	        if (indexRange->lower == 0) {
	            return -1;
	        }
        }
        else {
        	/* With step */
        	memcpy(tmpIndex, indexRangeString, colonIndex - indexRangeString);
        	tmpIndex[colonIndex - indexRangeString] = 0x00;
	        indexRange->lower = atoi(tmpIndex);
	        if (indexRange->lower == 0) {
	            return -1;
	        }
        }
        indexRange->upper = indexRange->lower;
        indexRange->step = 1;
    }
    else {
        /* Parse the lower limit */
        memcpy(tmpIndex, indexRangeString, dashIndex - indexRangeString);
        tmpIndex[dashIndex - indexRangeString] = 0x00;

        indexRange->lower = atoi(tmpIndex);
        if (indexRange->lower == 0) {
            indexRange->lower = START_JOB_INDEX;
        }

        /* Parse the upper limit. End sign is copied also. */
        if (colonIndex == NULL) {
        	/* Without step */
	        memcpy(tmpIndex, dashIndex + 1,
	               rangeLen - (dashIndex - indexRangeString));
	        indexRange->upper = atoi(tmpIndex);
	        if (indexRange->upper == 0) {
	            indexRange->upper = getMaxJobArraySize();
	        }
	        indexRange->step = 1;
        }
        else {
        	/* With step */
        	
        	/* Parse upper limit */
	        memcpy(tmpIndex, dashIndex + 1, colonIndex - dashIndex);
	        tmpIndex[colonIndex - dashIndex] = 0x00;
	        indexRange->upper = atoi(tmpIndex);
	        if (indexRange->upper == 0) {
	            indexRange->upper = getMaxJobArraySize();
	        }
	        
	        /* Parse step */
	        memcpy(tmpIndex, colonIndex + 1, rangeLen - (colonIndex - indexRangeString));
	        indexRange->step = atoi(tmpIndex);
	        if (indexRange->step == 0) {
	            indexRange->step = 1;
	        }
        }
    }

    free(tmpIndex);
    return 0;

}

/*
 * Append the given index to the job index list string.
 *
 * Params:
 *   jobIndexList - IN OUT Specify the job index list to end of which the index is appended.
 *   bufferLen - IN OUT Specify the total buffer len.
 *   currentPointer IN OUT Specify the current pointer position.
 *   indexRange - IN Specify the index to be appended.
 *   overlapped - IN 1, if the given index was possibly already in the list, otherwise, 0.
 * Return:
 *   0, if succeded, otherwise, -1.
 */
static int
appendToIndexList(int **jobIndexList, int *bufferLen, int **currentPointer,
                  IndexRange * indexRange, int overlapped)
{
    int index;
    int *preRangeTail = NULL;
    int *p = NULL;
    int indexExists;
    int usedBuffSize;           /* Currently used buffer size */
	int* tmp = NULL;
	int tailOffset;
	
	tailOffset = *currentPointer - *jobIndexList;
    preRangeTail = *jobIndexList + tailOffset;


    /* Append to list one by one */
    for (index = indexRange->lower; index <= indexRange->upper; index += indexRange->step) {
        if (overlapped) {
            indexExists = 0;

            /* Check if the current index exists already */
            for (p = *jobIndexList; p < preRangeTail; p++) {
                if (*p == index) {
                    indexExists = 1;
                    break;
                }
            }

            if (indexExists) {
                /* The current index exists in the list already */
                continue;
            }
        }

    	/* Check if the list is full. If so, reallocate memory */
    	usedBuffSize = *currentPointer - *jobIndexList;
		if (usedBuffSize >= *bufferLen) {
	        *jobIndexList =
	            (int *) realloc(*jobIndexList,
	                            (*bufferLen + INDEX_INCREMENT) * sizeof(int));
        
	        *bufferLen += INDEX_INCREMENT;
	        *currentPointer = *jobIndexList + usedBuffSize;
	        preRangeTail = *jobIndexList + tailOffset;
		}
		
        /* Append the index to the end of the list */
        **currentPointer = index;
        (*currentPointer) = (*currentPointer) + 1;
    }

    return 0;
}

/*
 * Generate job index list according to the given index list string.
 * Params:
 *   indexRangeListString - IN Specify a index list string.
 *   jobIndexList - OUT Return a list of index. The pointer need to be release after it is used.
 * Returns:
 *   0, if succeded, otherwise, -1.
 */
static int
generateJobIndexList(char *indexRangeListString, char **jobIndexList)
{
    int maxIndex = -1;
    int minIndex = -1;
    char *indexRangeString;
    int bufferLen;
    int *currentPointer = NULL;
    int *tmpJobIndexList = NULL;
    IndexRange indexRange;
    int delimiterLen;
    char *pChar = NULL;
    int overlapped = 0;
    int maxIndexLen;
    int jobCount = 1;
    int maxJobIndexListSize;
    int *pInt = NULL;

    /* Allocate memory for index list */
    tmpJobIndexList = malloc(INIT_INDEX_NUMBER * sizeof(int));
    if (tmpJobIndexList == NULL) {
        return -1;
    }
    /* Initialize the list */
    memset(tmpJobIndexList, 0x00, INIT_INDEX_NUMBER * sizeof(int));
    bufferLen = INIT_INDEX_NUMBER;
    currentPointer = tmpJobIndexList;

    /* Get length of delimiter */
    delimiterLen = strlen(INDEX_RANGE_DELIM);

    /* Pointer to the start of the list */
    indexRangeString = indexRangeListString;

    do {
        pChar = strstr(indexRangeString, INDEX_RANGE_DELIM);
        if (pChar != NULL) {
            *pChar = 0x00;
        }

        /* Parase the index range string */
        if (parseIndexRangeString
            (indexRangeString, &indexRange) == 0) {

            /* Check the range to see if it's overlapping with the existing one */
            overlapped = (indexRange.lower <= minIndex
                          && indexRange.upper >= maxIndex)
                || (indexRange.lower >= minIndex
                    && indexRange.lower <= maxIndex)
                || (indexRange.upper >= minIndex
                    && indexRange.upper <= maxIndex);

            /* Update the min index */
            if (minIndex > indexRange.lower || minIndex == -1) {
                minIndex = indexRange.lower;
            }

            /* Update the max index */
            if (maxIndex < indexRange.upper) {
                maxIndex = indexRange.upper;
            }

            /* Append the range to index list */
            if (appendToIndexList
                (&tmpJobIndexList, &bufferLen, &currentPointer,
                 &indexRange, overlapped) == -1) {

                free(tmpJobIndexList);
                return -1;
            }
        }

        if (pChar == NULL) {
            /* There is no more token */
            indexRangeString = NULL;
        }
        else {
            /* Pointer to the next token */
            indexRangeString = pChar + delimiterLen;
        }
    }
    while (indexRangeString != NULL);

    /* Get max length of the index */
    maxIndexLen = (int) log10(maxIndex) + 1;

    /* Get total job count */
    jobCount = (int) (currentPointer - tmpJobIndexList);
    maxJobIndexListSize = jobCount * (maxIndexLen + 1) * sizeof(char) + 1;

    /* Allocate memory for the job index list */
    *jobIndexList = malloc(maxJobIndexListSize);
    if (jobIndexList == NULL) {
    	free(tmpJobIndexList);
    	return -1;
    }
    memset(*jobIndexList, 0x00, maxJobIndexListSize);

    pChar = *jobIndexList;

    /* Convert index of integer to char */
    for (pInt = tmpJobIndexList; pInt != currentPointer; pInt++) {
        if (pInt != tmpJobIndexList) {
            sprintf(pChar, ",%d", *pInt);
        }
        else {
            sprintf(pChar, "%d", *pInt);
        }

        pChar += strlen(pChar);
    }

    /* Release memory of temporary job index list. */
    free(tmpJobIndexList);

    return jobCount;
}


/*
 * Count number of jobs and generate job array index list according to the given job name.
 * Params:
 *   jobName - IN Specify job name to be countted.
 *   jobIndexList - IN OUT Return a job array index list. The memory need to be release after it is used.
 * Returns:
 *   0, if succeded, otherwise, -1.
 */
extern int countJobByName(char *jobName, char **jobIndexList)
{
    char *rangeListString = NULL;
    int jobCount = 1;           /* Job number */
    /* Get range list string */
    rangeListString = getIndexRangeListString(jobName);
    if (rangeListString == NULL) {
        return jobCount;
    }
	
    /* Count number of jobs and generate job index list */
    jobCount = generateJobIndexList(rangeListString, jobIndexList);
	
    /* Free memory of rangeListString */
    free(rangeListString);

    return jobCount;

}
