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
 
 
#ifndef _JOB_ARRAY_H
#define _JOB_ARRAY_H

/*
 * Count number of jobs and generate job array index list according to the given job name.
 * Params:
 *   jobName - IN Specify job name to be countted.
 *   jobIndexList - IN OUT Return a job array index list. The memory need to be release after it is used.
 * Returns:
 *   0, if succeded, otherwise, -1.
 */
extern int countJobByName(char*, char**);

#endif 
