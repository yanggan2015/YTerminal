/* YTerminal - json.h : 极简 JSON 解析/序列化（UTF-8） */
#ifndef YT_JSON_H
#define YT_JSON_H

#include "util.h"

typedef enum { J_NULL = 0, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType type;
    int boolean;
    double num;
    char *str;
    JVal **items;
    int nitems, citems;
    char **keys;
    JVal **vals;
    int nkeys, ckeys;
};

JVal *json_parse(const char *text);
void  json_free(JVal *v);
JVal *json_new_obj(void);
JVal *json_new_arr(void);
JVal *json_new_str(const char *s);
JVal *json_new_num(double d);
JVal *json_new_bool(int b);

JVal *json_get(JVal *obj, const char *key);
const char *json_str(JVal *obj, const char *key, const char *def);
int   json_int(JVal *obj, const char *key, int def);
double json_num(JVal *obj, const char *key, double def);
int   json_bool(JVal *obj, const char *key, int def);

void json_set(JVal *obj, const char *key, JVal *val);   /* 接管所有权 */
void json_set_str(JVal *obj, const char *key, const char *s);
void json_set_int(JVal *obj, const char *key, int v);
void json_set_bool(JVal *obj, const char *key, int v);
void json_add(JVal *arr, JVal *val);                     /* 接管所有权 */

char *json_dump(JVal *v, int pretty);                    /* 调用者 free */

#endif
