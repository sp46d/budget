#include "record.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct record {
    char name[128];
    char date[64];
    float amount;
    size_t n_record;
    struct record* next;
} record_t;

// record_t* initial_point = NULL;

static record_t* find_last_record(record_t* initial_record)
{
    record_t* recordp;
    for (recordp = initial_record; recordp->next; recordp = recordp->next)
        ;
    return recordp;
}

record_t* record_init(void)
{
    record_t* initial_record = malloc(sizeof(record_t));
    initial_record->next = NULL;
    initial_record->n_record = 0;
    return initial_record;
}

void add_record(
    record_t* initial_record, const char* date, const char* name, float amount)
{
    record_t* current_data = malloc(sizeof(record_t));
    strlcpy(current_data->date, date, sizeof(current_data->date) - 1);
    strlcpy(current_data->name, name, sizeof(current_data->name) - 1);
    current_data->amount = amount;
    current_data->next = NULL;
    record_t* recordp = find_last_record(initial_record);
    current_data->n_record = recordp->n_record + 1;
    recordp->next = current_data;
}

float lookup_amount(
    record_t* initial_record, const char* date, const char* name)
{
    record_t* recordp;
    float result = 0.0f;
    for (recordp = initial_record->next; recordp; recordp = recordp->next) {
        if (strcmp(recordp->date, date) == 0
            && strcmp(recordp->name, name) == 0) {
            result = recordp->amount;
            break;
        }
    }
    return result;
}

record_t* next_record(record_t* current_record) { return current_record->next; }

const char* get_date(record_t* current_record)
{
    if (current_record->n_record == 0) {
        return NULL;
    } else {
        return current_record->date;
    }
}

const char* get_name(record_t* current_record)
{
    if (current_record->n_record == 0) {
        return NULL;
    } else {
        return current_record->name;
    }
}

size_t get_id(record_t* current_record) { return current_record->n_record; }
void free_records(record_t* current_record)
{
    record_t* recordp = current_record;
    while (recordp) {
        record_t* temp = recordp->next;
        free(recordp);
        recordp = temp;
    }
    printf("freed up all items\n");
}
