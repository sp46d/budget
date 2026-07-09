#include "record.h"
#include <math.h>
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
    strlcpy(
        current_data->date, date ? date : "", sizeof(current_data->date) - 1);
    strlcpy(current_data->name, name, sizeof(current_data->name) - 1);
    current_data->amount = amount;
    current_data->next = NULL;
    record_t* recordp = find_last_record(initial_record);
    current_data->n_record = get_id(recordp) + 1;
    recordp->next = current_data;
}

float lookup_amount(
    record_t* initial_record, const char* date, const char* name)
{
    record_t* recordp;
    float result = 0.0f;
    for (recordp = initial_record; recordp; recordp = recordp->next) {
        if (get_id(recordp) == 0) {
            continue;
        }
        if (!*date && strcmp(get_name(recordp), name) == 0) {
            result = recordp->amount;
            break;
        } else if (strcmp(get_date(recordp), date) == 0
            && strcmp(get_name(recordp), name) == 0) {
            result = recordp->amount;
            break;
        }
    }
    return result;
}

record_t* next_record(record_t* current_record)
{
    if (!current_record) {
        return NULL;
    }
    return current_record->next;
}

const char* get_date(record_t* current_record)
{
    if (get_id(current_record) == 0) {
        return NULL;
    } else {
        return current_record->date;
    }
}

const char* get_name(record_t* current_record)
{
    if (get_id(current_record) == 0) {
        return NULL;
    } else {
        return current_record->name;
    }
}

size_t get_id(record_t* current_record)
{
    if (!current_record) {
        return 0;
    }
    return current_record->n_record;
}

double average_by_name(record_t* initial_record, const char* name)
{
    double sum = 0.0;
    int n = 0;
    for (record_t* recordp = initial_record; recordp; recordp = recordp->next) {
        if (get_id(recordp) == 0) {
            continue;
        }
        if (strcmp(get_name(recordp), name) == 0) {
            sum += recordp->amount;
            n++;
        }
    }
    return sum / (double)n;
}

double stdev_by_name(record_t* initial_record, const char* name)
{
    double mean = average_by_name(initial_record, name);
    double variance_sum = 0.0;
    int n = 0;
    for (record_t* recordp = initial_record; recordp; recordp = recordp->next) {
        if (get_id(recordp) == 0) {
            continue;
        }
        if (strcmp(get_name(recordp), name) == 0) {
            variance_sum += pow(recordp->amount - mean, 2);
            n++;
        }
    }
    return sqrt(variance_sum / (double)n);
}

void free_records(record_t* initial_record)
{
    record_t* recordp = initial_record;
    while (recordp) {
        record_t* temp = recordp->next;
        free(recordp);
        recordp = temp;
    }
    printf("freed up all items\n");
}
