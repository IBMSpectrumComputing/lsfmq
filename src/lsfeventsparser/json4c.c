/************************************************************************
 *
 * JSON4C
 *
 * json4c.c -- jianjin, 2016-07-20
 *
 * A C implement JSON util for perf loader bundle with filebeat in logstash
 *
 ************************************************************************/

#include "strreplace.h"
#include "json4c.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef FREEUP
#define FREEUP(p)                                                              \
  {                                                                            \
    if (p)                                                                     \
      free(p);                                                                 \
    p = NULL;                                                                  \
  }
#endif

#define IS_DOUBLE 0
#define IS_INTEGER 1

#if defined(DEBUG)
#define TRACE(...)	printf(__VA_ARGS__)
#else
#define TRACE(...)	
#endif

int kstrncat(char **dest, char *src);
int ksnprintf(char **target, char *format, ...);
char *stringToString(Json4c *string);
char *numberToString(Json4c *number);
char *arrayToString(Json4c *array);
char *objectToString(Json4c *object);
void addChild(Json4c *instance, Json4c *child);
void objectFree(Json4c *object);
void arrayFree(Json4c *array);
void numberFree(Json4c *number);
void stringFree(Json4c *string);
int isInteger(double num);

// Create JSON object
Json4c *jCreateObject() {
	Json4c *object = (Json4c *) malloc(sizeof(Json4c));
	if (object) {
		memset(object, 0, sizeof(Json4c));
		object->type = Json4cObject;
	}
	return object;
}

// Create JSON Array
Json4c *jCreateArray() {
	Json4c *array = (Json4c *) malloc(sizeof(Json4c));
	if (array) {
		memset(array, 0, sizeof(Json4c));
		array->type = Json4cArray;
	}
	return array;
}

// Create a Json4c instance for number
Json4c *createNumber() {
	Json4c *number = (Json4c *) malloc(sizeof(Json4c));
	if (number) {
		memset(number, 0, sizeof(Json4c));
		number->type = Json4cNumber;
	}
	return number;
}

// Create a Json4c instance for string
Json4c *createString() {
	Json4c *string = (Json4c *) malloc(sizeof(Json4c));
	if (string) {
		memset(string, 0, sizeof(Json4c));
		string->type = Json4cString;
	}
	return string;
}

// Add number to a JSON object
void addNumberToObject(Json4c *object, const char *key, double value) {
	if (!object || !key) {
		return;
	}

	Json4c *number = createNumber();
	if (!number) {
		return;
	}

	ksnprintf(&number->key, "%s", key);

	number->valuedouble = value;

	addChild(object, number);
}

// Add string to a JSON object
void addStringToObject(Json4c *object, const char *key, char *value) {
	if (!object || !key) {
		return;
	}

	// Added by ZK on 2016-08-12 to filter out the key-pair value when value is null
	if(!value){
		return;
	}

	Json4c *string = createString();
	if (!string) {
		return;
	}

    char *str = strreplace(value, "\"", "\\\"");
    str = strreplace(str, "\'", "\\\'");

	ksnprintf(&string->key, "%s", key);
	ksnprintf(&string->valuestring, "%s", str);

	addChild(object, string);
	free(str)
}

void addInstanceToObject(Json4c *object, const char *key, Json4c *instance) {
	if (!object || !key || !instance) {
		return;
	}

	ksnprintf(&instance->key, "%s", key);

	addChild(object, instance);
}

// Add number to a JSON array
void addNumberToArray(Json4c *array, double value) {
	if (!array) {
		return;
	}

	Json4c *number = createNumber();
	if (!number) {
		return;
	}

	number->valuedouble = value;

	addChild(array, number);
}

// Add string to a JSON array
void addStringToArray(Json4c *array, char *value) {
	if (!array) {
		return;
	}

	Json4c *string = createString();
	if (!string) {
		return;
	}

	ksnprintf(&string->valuestring, "%s", value);
	addChild(array, string);
}

// Add JSON object to a JSON array
void addInstanceToArray(Json4c *array, Json4c *instance) {
	if (!array || !instance) {
		return;
	}
	addChild(array, instance);
}

// To JSON String
// User should delete the returned char* manually, otherwise, there will be a
// memory leak.
char *jToString(Json4c *instance) {
	if (!instance) {
		return NULL;
	}

	switch (instance->type) {
	case Json4cUndefined:
		return NULL;
	case Json4cObject:
		return (char *) objectToString(instance);
	case Json4cArray:
		return (char *) arrayToString(instance);
	case Json4cNumber:
		return (char *) numberToString(instance);
	case Json4cString:
		return (char *) stringToString(instance);
	}
	return NULL;
}

// Deep clean current Json4c object.
void jFree(Json4c *instance) {
	if (!instance) {
		return;
	}

	switch (instance->type) {
	case Json4cUndefined:
		FREEUP(instance)
		;
		break;
	case Json4cObject:
		objectFree(instance);
		break;
	case Json4cArray:
		arrayFree(instance);
		break;
	case Json4cNumber:
		numberFree(instance);
		break;
	case Json4cString:
		stringFree(instance);
		break;
	}
}

void objectFree(Json4c *object) {
	if (!object) {
		return;
	}

	if (object->key) {
		FREEUP(object->key);
	}

	Json4c *child = object->valuechild;
	if (child) {
		Json4c *prev;
		while (child) {
			prev = child;
			child = child->next;
			jFree(prev);
		}
	}
	FREEUP(object);
}

void arrayFree(Json4c *array) {
	if (!array) {
		return;
	}

	if (array->key) {
		FREEUP(array->key);
	}

	Json4c *child = array->valuechild;
	if (child) {
		Json4c *prev;
		while (child) {
			prev = child;
			child = child->next;
			jFree(prev);
		}
	}
	FREEUP(array);
}

void numberFree(Json4c *number) {
	if (!number) {
		return;
	}

	FREEUP(number->key);
	FREEUP(number);
}

void stringFree(Json4c *string) {
	if (!string) {
		return;
	}

	FREEUP(string->key);
	FREEUP(string->valuestring);
	FREEUP(string);
}

// Add a child to the end of object/array's child list
void addChild(Json4c *instance, Json4c *child) {
	if (!instance || !child) {
		return;
	}

	if (Json4cObject != instance->type && Json4cArray != instance->type) {
		return;
	}

	if (!instance->valuechild) {
		instance->valuechild = child;
	} else {
		Json4c *prev = NULL;
		Json4c *last = instance->valuechild;
		while (last) {
			prev = last;
			last = last->next;
		}
		prev->next = child;
		child->prev = prev;
		child->next = NULL;
	}
}

char *objectToString(Json4c *object) {
	char *ret = NULL;
	if (!object->key) {
		ksnprintf(&ret, "{");
	} else {
		TRACE("stringify object with key %s\n", object->key);
		ksnprintf(&ret, "\"%s\":{", object->key);
	}

	if (!object || Json4cObject != object->type) {
		kstrncat(&ret, "}");
		return ret;
	}

	Json4c *child = object->valuechild;
	if (!child) {
		kstrncat(&ret, "}");
		return ret;
	} else {
		Json4c *prev;
		while (child) {
			char *buffer = jToString(child);
			kstrncat(&ret, buffer);
			kstrncat(&ret, ",");
			prev = child;
			child = child->next;
			FREEUP(buffer);
		}
		ret[strlen(ret) - 1] = '\0'; // Remove the last comma
		kstrncat(&ret, "}");
		return ret;
	}
}

char *arrayToString(Json4c *array) {
	char *ret = NULL;
	if (!array->key) {
		ksnprintf(&ret, "[");
	} else {
		ksnprintf(&ret, "\"%s\":[", array->key);
	}

	if (!array || Json4cArray != array->type) {
		kstrncat(&ret, "]");
		return ret;
	}

	Json4c *child = array->valuechild;
	if (!child) {
		kstrncat(&ret, "]");
		return ret;
	} else {
		Json4c *prev;
		while (child) {
			char *buffer = jToString(child);
			kstrncat(&ret, buffer);
			kstrncat(&ret, ",");
			prev = child;
			child = child->next;
			FREEUP(buffer);
		}
		ret[strlen(ret) - 1] = '\0'; // Remove the last comma
		kstrncat(&ret, "]");
		return ret;
	}
}

char *numberToString(Json4c *number) {
	char *ret = NULL;

	if (!number || Json4cNumber != number->type) {
		return ret;
	}

	if (IS_INTEGER == isInteger(number->valuedouble)) {
		if (!number->key) {
			ksnprintf(&ret, "%d", (int) number->valuedouble);
		} else {
			ksnprintf(&ret, "\"%s\":%d", number->key,
					(int) number->valuedouble);
		}
	} else {
		if (!number->key) {
			ksnprintf(&ret, "%lf", number->valuedouble);
		} else {
			ksnprintf(&ret, "\"%s\":%lf", number->key, number->valuedouble);
		}
	}

	return ret;
}

char *stringToString(Json4c *string) {
	char *ret = NULL;

	if (!string || Json4cString != string->type) {
		return ret;
	}

	if (!string->key) {
		ksnprintf(&ret, "\"%s\"", string->valuestring);
	}else {
		ksnprintf(&ret, "\"%s\":\"%s\"", string->key, string->valuestring);
	}
	return ret;
}

/**
 * jian jin, 2013-05-07, easy use for vsnprintf
 */
int ksnprintf(char **target, char *format, ...) {
	va_list ap;

	static int initLen = 512;
	int realLen = initLen;
	int len = initLen;
	int index = 1;

	while (len >= realLen) {
		FREEUP(*target);
		realLen = initLen * index++;
		*target = malloc(realLen * sizeof(char));
                memset(*target, 0, realLen * sizeof(char));

		va_start(ap, format);
		len = vsnprintf(*target, realLen, format, ap);
		va_end(ap);
	}

	return len;
}

/**
 * jian jin, 2016-7-20, easy use for strncat
 *
 * Return:
 * 	0, no copy from src to dest
 * 	1, full copy from src to dest
 */
int kstrncat(char **dest, char *src) {

	if (!src) {
		return 0;
	}

	char *buffer = NULL;
	if (!*dest) {
		ksnprintf(&buffer, "%s", src);
	} else {
		ksnprintf(&buffer, "%s%s", *dest, src);
	}
	FREEUP(*dest);
	*dest = buffer;
	return 1;
}

/**
 * jian jin, 2016-7-20, detect if a double variable contains an integer
 *
 * Return:
 * 	IS_DOUBLE, no, it's a double value.
 * 	IS_INTEGER, yes, it's an integer, can be cast into int
 */
int isInteger(double num) {
	if ((int) num == num) {
		return IS_INTEGER;
	} else {
		return IS_DOUBLE;
	}
}

int main() {
	Json4c *test1 = jCreateArray();
	Json4c *test = jCreateObject();
	addNumberToArray(test1, 12);
	addStringToArray(test1, "asd");
	addInstanceToObject(test, "haha", test1);
	char *buffer = jToString(test);
	jFree(test);
	printf("%s \n", buffer);
	free(buffer);
	return 1;
}
