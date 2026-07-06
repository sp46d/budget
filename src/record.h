#ifndef RECORD_H
#define RECORD_H

#include <stdio.h>
typedef struct record record_t;

record_t* record_init(void);
void add_record(
    record_t* initial_record, const char* date, const char* name, float amount);
float lookup_amount(
    record_t* initial_record, const char* date, const char* name);
record_t* next_record(record_t* current_record);
const char* get_date(record_t* current_record);
const char* get_name(record_t* current_record);
size_t get_id(record_t* current_record);
void free_records(record_t* current_record);

#endif
