/************************************************************************
 *
 * JSON4C
 *
 * json4c.h -- jianjin, 2016-07-20
 *
 * A C implement JSON util for perf loader bundle with filebeat in logstash
 *
 ************************************************************************/

#ifndef _JSON4C_H_
#define _JSON4C_H_

#ifdef __cplusplus
extern "C" {
#endif

// types
#define Json4cUndefined 0
#define Json4cObject 1
#define Json4cArray 2
#define Json4cNumber 4
#define Json4cString 8

// json4c
typedef struct Json4c {
  // json4c object type, could be Json4cObject Json4cArray Json4cNumber or
  // Json4cString
  int type;

  // If this json4c object is a Json4cObject or Json4cArray, the prev and next
  // pointer should be NULL.
  // If this json4c object is a Json4cNumber or Json4cString, the prev and next
  // pointer should point to previous and next Json4cNumber or Json4cString.
  // The first one's prev should be NULL.
  // The last one's next should be NULL.
  struct Json4c *prev;
  struct Json4c *next;

  // JSON object key
  char *key;

  // JSON object value
  char *valuestring;
  double valuedouble;
  // If this json4c object is a Json4cObject or Json4cArray, the child
  // pointer
  // should point to the first value it has.
  // If this json4c object is a Json4cNumber or Json4cString, the child
  // pointer
  // should be NULL.
  struct Json4c *valuechild;
} Json4c;

// To JSON String
// User should delete the returned char* manually, otherwise, there will be a
// memory leak.
char *jToString(Json4c *instance);

// Deep clean current Json4c object.
void jFree(Json4c *instance);

// Create JSON instance
// Remenber to call free after use, otherwise, there will be memory leak.
// Only call free() function for root instance, the function will do a deep
// clean for all children in the root instance.
Json4c *jCreateObject();
Json4c *jCreateArray();

// Add value to JSON instance
void addNumberToObject(Json4c *object, const char *key, double value);
void addStringToObject(Json4c *object, const char *key, char *value);
void addInstanceToObject(Json4c *object, const char *key, Json4c *instance);
void addNumberToArray(Json4c *array, double value);
void addStringToArray(Json4c *array, char *value);
void addInstanceToArray(Json4c *array, Json4c *instance);

#ifdef __cplusplus
}
#endif

#endif
