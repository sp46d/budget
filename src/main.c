// DONE: Finish query_budget function
// DONE: Check duplicate items and retain only one and discard the other
// DONE: Work on import logic flow, so that I can feed the raw statement files
// as is (apply awk parser on the way)
// DONE: git the code and upload it to github
// DONE: refactor the way it gets query from user
// DONE: Switch from fgets() to readline()
// WORKING: Add more categories and rules
// DONE: Add monthly/yearly summary report
// TODO: Clean up all functions and codes
// TODO: Split codes into multiple files
// TODO: Add forecast report
// TODO: Allow for modifying categories and payees and their rule patterns
// TODO: Cache categories and payee

#include "record.h"
#include <ctype.h>
#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <regex.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DB "budget.db"
#define TOTAL_MONTHS 24
#define TOTAL_CATEGORIES 128
#define SUMMARY_COLUMNS 6

typedef struct {
    char sql_stmt[1536];
    char custom[256];
    char date[64];
    int cat_id;
    int payee_id;
    char limit[8];
} st_query;

typedef enum {
    MEAN,
    STDEV,
} stat_type;

typedef enum {
    YEAR,
    MONTH,
    DAY,
} date_type;

typedef const char* (*getter)(record_t*);

bool validate_input(const char* input, const char* pattern);
sqlite3* open_db(const char* db_name);
bool check_file(const char* buf);
void import_csv(char* filename);
void retrieve_or_report(void);
void query_budget(void);
void summary_prompt(void);
void summary_budget(int cat_id);
void summary_by_payee(void);
void add_date_filter(char* stmt, char* date);
void add_cat_filter(char* stmt, int cat_id);
void add_payee_filter(char* stmt, int payee_id);
void add_limit_filter(char* stmt, char* n_limits);
void compose_sql_stmt(st_query* user_query);
void compose_summary_stmt(st_query* user_stmt);
int unique_data(
    record_t* tr_data, getter get_data, const char* result[], int len);
// int unique_names(record_t* tr_data, const char* names[], int len);
void print_query(st_query* user_query);
void print_summary(st_query* user_stmt);
void forecast_prompt(void);
void print_forecast_fixed(const char* date);
void print_past_trend(const char* date);
void delete_duplicates(void);
void update_categories(void);
int get_today_date(date_type type);
void import_bank_stmt(void);
void parse_txt(char* filename);
void category_prompt(void);
void add_categories(void);
int print_list_with_id(sqlite3** db, int list_idx, int cat_id);
record_t* get_statistics(record_t* tr_data, stat_type type);
void print_statistics(void);
void view_rules(void);
bool is_valid_int(const char* string);
static int get_id_callback(void* data, int argc, char** argv, char** azColName);
static int add_item(sqlite3** db, const char* input, const char* tablename);
static int print_items(sqlite3** db, const char* tablename, int cat_id);

int main(void)
{
    char* input;
    const char* prompt
        = "\n[I]mport statements  [R]etrieve/reports  [C]ategories  "
          "[U]pdate database\nbudget> ";
    while (1) {
        if ((input = readline(prompt)) != NULL) {
            if (*input) {
                add_history(input);

                if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
                    free(input);
                    break;
                } else if (strcmp(input, "i") == 0 || strcmp(input, "I") == 0) {
                    import_bank_stmt();
                    delete_duplicates();
                    update_categories();
                } else if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0) {
                    retrieve_or_report();
                } else if (strcmp(input, "u") == 0 || strcmp(input, "U") == 0) {
                    // refreshing tables means that it deletes all duplicate
                    // rows and categorize transactions that have not yet been
                    // categorized.
                    delete_duplicates();
                    update_categories();
                } else if (strcmp(input, "c") == 0 || strcmp(input, "C") == 0) {
                    category_prompt();
                }
            }
        }
        free(input);
    }
    return 0;
}

bool validate_input(const char* input, const char* pattern)
{
    // copy input to buf for validation
    char buf[128];
    strlcpy(buf, input, sizeof(buf) - 1);
    buf[strlen(buf)] = '\n';

    regex_t regex;
    int return_value;
    return_value
        = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);
    if (return_value != 0) {
        fprintf(stderr, "regcomp: can't compile regex pattern\n");
        exit(1);
    }
    return_value = regexec(&regex, buf, 0, NULL, REG_NOTEOL);
    regfree(&regex);
    if (return_value == 0) {
        // pattern matched!
        return true;
    } else if (return_value == REG_NOMATCH) {
        // pattern not matched!
        return false;
    } else {
        // something goes wrong
        return false;
    }
}

void import_bank_stmt(void)
{
    char* filename;
    const char* prompt
        = "\n[Enter file name]\n[M]ain menu\n-----------------\n> ";
    while (1) {
        if ((filename = readline(prompt)) != NULL) {
            if (*filename) {
                add_history(filename);
            }
            if (strcmp(filename, "exit") == 0
                || strcmp(filename, "quit") == 0) {
                free(filename);
                exit(0);
            } else if (strcmp(filename, "m") == 0
                || strcmp(filename, "M") == 0) {
                free(filename);
                break;
            } else if (check_file(filename)) {
                import_csv(filename);
                printf("\n>> \"%s\" successfully imported\n", filename);
                free(filename);
                break;
            }
        }
        free(filename);
    }
}

bool check_file(const char* filename)
{
    // check file extension
    if (!strstr(filename, ".txt") && !strstr(filename, ".csv")) {
        printf("Not a txt or csv file\n");
        return false;
    }
    // Look up given file name
    FILE* fp = fopen(filename, "r");
    if (fp) {
        fclose(fp);
        return true;
    }
    printf("No such file\n");
    return false;
}

void parse_txt(char* filename)
{
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (child == 0) {
        // child process
        int fd = open("tmp.csv", O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        dup2(fd, STDOUT_FILENO);
        close(fd);
        char* args[] = { "sh", "./budget.sh", filename, NULL };
        execvp(args[0], args);
    }

    int status;
    waitpid(child, &status, 0);
}

void import_csv(char* filename)
{
    sqlite3* db = open_db(DB);
    char* csv_file = filename;
    if (strstr(filename, ".txt")) {
        parse_txt(filename);
        csv_file = "tmp.csv";
    }
    // Import statement from CSV
    FILE* fp = fopen(csv_file, "r");
    char line[1024];
    int row = 0;
    sqlite3_stmt* out_stmt;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char* in_stmt
        = "INSERT INTO transactions (date, year, month, day, "
          "description, description_n, amount, payee_id, cat_id)"
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, in_stmt, -1, &out_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare error: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    while (fgets(line, sizeof(line), fp)) {
        row++;
        char* linep = line;
        char* date = strsep(&linep, ",");
        char* year = strsep(&linep, ",");
        char* month = strsep(&linep, ",");
        char* day = strsep(&linep, ",");
        char* desc = strsep(&linep, ",");
        char* desc_n = strsep(&linep, ",");
        char* amount = strsep(&linep, ",");
        char* payee_id = strsep(&linep, ",");
        char* cat_id = strsep(&linep, "\n");

        sqlite3_bind_text(out_stmt, 1, date, -1, NULL);
        sqlite3_bind_int(out_stmt, 2, atoi(year));
        sqlite3_bind_int(out_stmt, 3, atoi(month));
        sqlite3_bind_int(out_stmt, 4, atoi(day));
        sqlite3_bind_text(out_stmt, 5, desc, -1, NULL);
        sqlite3_bind_text(out_stmt, 6, desc_n, -1, NULL);
        sqlite3_bind_double(out_stmt, 7, atof(amount));
        sqlite3_bind_int(out_stmt, 8, atoi(payee_id));
        sqlite3_bind_int(out_stmt, 9, atoi(cat_id));

        if (sqlite3_step(out_stmt) != SQLITE_DONE) {
            fprintf(stderr, "import error in row %d: %s\n", row,
                sqlite3_errmsg(db));
            exit(1);
        }
        sqlite3_reset(out_stmt);
        sqlite3_clear_bindings(out_stmt);
    }

    sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    sqlite3_finalize(out_stmt);
    sqlite3_close(db);
    if (strcmp(csv_file, "tmp.csv") == 0) {
        remove("tmp.csv");
    }
}

void retrieve_or_report(void)
{
    char* prompt = "\n[R]trieve records\n[S]ummary report\n[F]orecast report\n"
                   "[M]ain menu\n-----------------\n> ";
    while (1) {
        char* input = readline(prompt);
        if (input != NULL) {
            if (*input) {
                add_history(input);
            }
            if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
                free(input);
                exit(0);
            } else if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0) {
                query_budget();
                free(input);
                break;
            } else if (strcmp(input, "s") == 0 || strcmp(input, "S") == 0) {
                // printf("Summary report chosen!\n");
                summary_prompt();
                free(input);
                break;
            } else if (strcmp(input, "f") == 0 || strcmp(input, "F") == 0) {
                forecast_prompt();
                free(input);
                break;
            } else if (strcmp(input, "m") == 0 || strcmp(input, "M") == 0) {
                free(input);
                break;
            }
        }
        free(input);
    }
}

sqlite3* open_db(const char* db_name)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            db_name, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    return db;
}

void query_budget(void)
{
    st_query user_query;
    sqlite3* db = open_db(DB);
    int cat_id = -1;
    const char* prompts[] = { "\n\nCategory [number]: ", "\n\nPayee [Number]: ",
        "Date: ", "Number of items: ", "Other filters: ", NULL };
    for (int i = 0; prompts[i]; i++) {
        if (i == 0) {
            print_list_with_id(&db, 0, -1);
        } else if (i == 1) {
            if (cat_id > 0) {
                print_list_with_id(&db, 1, cat_id);
            } else {
                user_query.payee_id = -1;
                continue;
            }
        }
        char* query = readline(prompts[i]);
        if (query != NULL) {
            if (*query) {
                add_history(query);
            }
            if (i == 0) {
                if (!*query) {
                    user_query.cat_id = -1;
                    continue;
                }
                if (!is_valid_int(query)) {
                    printf("!! Not a valid number !!\n");
                    free(query);
                    return;
                }
                cat_id = atoi(query);
                user_query.cat_id = cat_id;
            } else if (i == 1) {
                if (!*query) {
                    user_query.payee_id = -1;
                    continue;
                }
                if (!is_valid_int(query)) {
                    printf("!! Not a valid number !!\n");
                    free(query);
                    return;
                }
                user_query.payee_id = atoi(query);
            } else if (i == 2) {
                if (validate_input(query, "^[0-9-]{1,10}[,]?[ ]*[0-9-]{0,10}$")
                    || *query == '\0') {
                    strlcpy(
                        user_query.date, query, sizeof(user_query.date) - 1);
                } else {
                    printf("\n!! Not a valid input for date !!\n");
                    free(query);
                    return;
                }
            } else if (i == 3) {
                if (!is_valid_int(query)) {
                    printf("!! Not a valid number !!\n");
                    free(query);
                    return;
                }
                strlcpy(user_query.limit, query, sizeof(user_query.limit) - 1);
            } else if (i == 4) {
                strlcpy(
                    user_query.custom, query, sizeof(user_query.custom) - 1);
            }
        }
        free(query);
    }
    sqlite3_close(db);
    compose_sql_stmt(&user_query);
    print_query(&user_query);
}

void summary_prompt(void)
{
    char* prompt
        = "\nBy [C]ategory  or  [P]ayee\n--------------------------\n> ";
    while (1) {
        char* input = readline(prompt);
        if (input != NULL) {
            if (*input) {
                add_history(input);
            }
            if (strcmp(input, "c") == 0 || strcmp(input, "C") == 0) {
                summary_budget(-1);
                free(input);
                break;
            }
            if (strcmp(input, "p") == 0 || strcmp(input, "P") == 0) {
                summary_by_payee();
                free(input);
                break;
            }
        }
        free(input);
    }
}

void summary_by_payee(void)
{
    sqlite3* db = open_db(DB);
    print_list_with_id(&db, 0, -1);
    sqlite3_close(db);

    while (1) {
        char* input = readline("\n\nCategory [number]: ");
        if (input != NULL) {
            if (*input) {
                add_history(input);
                summary_budget(atoi(input));
                free(input);
                break;
            }
        }
        free(input);
    }
}

void summary_budget(int cat_id)
{
    st_query user_stmt;
    user_stmt.cat_id = cat_id;
    while (1) {
        char* input = readline("\nDate for summary: ");
        if (input != NULL) {
            if (*input) {
                add_history(input);
                if (validate_input(input, "^[0-9]{4}-[0-9]{1,2}$")
                    || validate_input(input, "^[0-9]{4}$")
                    || validate_input(input, "^[0-9]{1,2}$")
                    || validate_input(input, "^[0-9]{1,2}[,][0-9]{1,2}$")
                    || validate_input(
                        input, "^[0-9]{4}-[0-9]{1,2}[,][0-9]{4}-[0-9]{1,2}$")) {
                    strlcpy(user_stmt.date, input, sizeof(user_stmt.date) - 1);
                    free(input);
                    break;
                } else {
                    printf("\n!! Not a valid input for summary !!\n");
                }
            }
        }
        free(input);
    }
    compose_summary_stmt(&user_stmt);
    print_summary(&user_stmt);
}

void compose_summary_stmt(st_query* user_stmt)
{
    char sql_stmt[1024]
        = "SELECT strftime('%Y-%m', t.date) AS date_y_m, "
          "sum(t.amount) AS amount, c.name as category, p.name "
          "FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id =c.id "
          "LEFT OUTER JOIN payee p ON t.payee_id = p.id ";
    add_date_filter(sql_stmt, user_stmt->date);

    if (user_stmt->cat_id > 0) {
        char more_stmt[256];
        snprintf(more_stmt, sizeof(more_stmt),
            " AND t.cat_id = %d AND p.name NOT LIKE 'mortgage' GROUP BY "
            "date_y_m, p.id ORDER BY date_y_m, p.id;",
            user_stmt->cat_id);
        strlcat(sql_stmt, more_stmt, sizeof(sql_stmt) - strlen(sql_stmt) - 1);
    } else {
        strlcat(sql_stmt,
            " AND t.cat_id != 0 AND p.name NOT LIKE 'mortgage' GROUP BY "
            "date_y_m, category ORDER BY date_y_m, category;",
            sizeof(sql_stmt) - strlen(sql_stmt) - 1);
    }
    strlcpy(user_stmt->sql_stmt, sql_stmt, sizeof(user_stmt->sql_stmt) - 1);
}

int unique_data(
    record_t* tr_data, getter get_data, const char* result[], int len)
{
    int idx = 0;
    for (record_t* recordp = tr_data; recordp != NULL;
        recordp = next_record(recordp)) {
        if (get_id(recordp) == 0) {
            continue;
        }
        const char* data = get_data(recordp);
        bool exist = false;
        for (int i = 0; i < idx; i++) {
            if (strcmp(data, result[i]) == 0) {
                exist = true;
                break;
            }
        }
        if (!exist) {
            if (idx < len - 1) {
                result[idx] = data;
                idx++;
            } else {
                break;
            }
        }
    }
    result[idx] = NULL;
    return idx;
}

void print_summary(st_query* user_stmt)
{
    bool by_payee = user_stmt->cat_id > 0;
    sqlite3* db = open_db(DB);
    const char* sql_stmt = (const char*)user_stmt->sql_stmt;
    // printf("\n> SQL Statement: %s\n\n", sql_stmt);

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }

    // Fetch data
    record_t* tr_data = record_init();
    char header_cat[128];
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const char* date = (const char*)sqlite3_column_text(prepared_stmt, 0);
        float amount = (float)sqlite3_column_double(prepared_stmt, 1);
        const char* category
            = (const char*)sqlite3_column_text(prepared_stmt, 2);
        const char* payee = (const char*)sqlite3_column_text(prepared_stmt, 3);
        if (by_payee) {
            add_record(tr_data, date, payee, amount);
            snprintf(header_cat, sizeof(header_cat), "%s", category);
        } else {
            add_record(tr_data, date, category, amount);
            snprintf(header_cat, sizeof(header_cat), "CATEGORY");
        }
    }

    // Get unique months and categories
    const char* months[TOTAL_MONTHS + 1];
    const char* names[TOTAL_CATEGORIES + 1];
    int nmonths = unique_data(tr_data, get_date, months, TOTAL_MONTHS + 1);
    unique_data(tr_data, get_name, names, TOTAL_CATEGORIES + 1);

    // Print result
    for (int row = 0; row < (nmonths + SUMMARY_COLUMNS - 1) / SUMMARY_COLUMNS;
        row++) {
        float sum[SUMMARY_COLUMNS] = { 0.0f };
        char tb_hline[512] = "-";
        int row_idx = row * SUMMARY_COLUMNS;
        printf("\n%20s", header_cat);
        strlcat(tb_hline, "--------------------",
            sizeof(tb_hline) - strlen(tb_hline) - 1);
        for (int i = 0; months[row_idx + i] && i < SUMMARY_COLUMNS; i++) {
            printf("   %10s", months[row_idx + i]);
            strlcat(tb_hline, "-------------",
                sizeof(tb_hline) - strlen(tb_hline) - 1);
        }
        printf("\n%s\n", tb_hline);

        int j;
        for (int i = 0; names[i]; i++) {
            printf("%20s", names[i]);
            for (j = 0; months[row_idx + j] && j < SUMMARY_COLUMNS; j++) {
                float amount
                    = lookup_amount(tr_data, months[row_idx + j], names[i]);
                printf("   %10.2f", amount);
                sum[j] += amount;
            }
            printf("\n");
        }
        printf("%s\n", tb_hline);
        printf("%20s", "TOTAL");
        for (int i = 0; i < j; i++) {
            printf("   %10.2f", sum[i]);
        }
        printf("\n%s\n", tb_hline);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void forecast_prompt(void)
{
    // st_query user_stmt;
    char* input;
    while (true) {
        if ((input = readline("\nDate (YYYY-MM-DD): ")) != NULL) {
            if (*input) {
                add_history(input);
            }
            if (validate_input(input, "^[0-9]{4}[-][0-9]{1,2}[-][0-9]{1,2}$")) {
                // strlcpy(user_stmt.date, input, sizeof(user_stmt.date) - 1);
                print_forecast_fixed(input);
                print_past_trend(input);
                free(input);
                break;
            } else {
                printf("\n!! Not a valid date format !!\n");
            }
        }
        free(input);
    }
}

void print_forecast_fixed(const char* date)
{
    sqlite3* db = open_db(DB);
    char* datep;
    char* tofree;
    tofree = datep = strdup(date);
    int year = atoi(strsep(&datep, "-"));
    int month = atoi(strsep(&datep, "-"));
    int day = atoi(strsep(&datep, "-"));
    free(tofree);

    char sql_stmt[1536];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "SELECT p.name, min(t.day) AS earliest, max(t.day) AS latest, "
        "avg(t.amount) AS avg_amount FROM transactions t LEFT OUTER JOIN "
        "payee p ON t.payee_id = p.id LEFT OUTER JOIN categories c ON "
        "p.cat_id = c.id WHERE t.date BETWEEN '2026-01-01' AND '2026-05-31' "
        "AND c.name = 'fixed' AND p.name != 'Mortgage' AND t.amount < -9 GROUP "
        "BY p.name HAVING latest > %d ORDER BY earliest;",
        day);

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }

    printf(
        "\n[[ Forecast Report as of '%4d-%02d-%02d' ]]\n\n", year, month, day);
    printf("1. Upcoming fixed expenses\n\n");
    printf("---------------------------------------------------------\n");
    printf("     Due Date\n");
    printf(" Earliest  Latest    ITEMS                 AMOUNT (avg)\n");
    printf("---------------------------------------------------------\n");
    float total = 0.0f;
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const char* payee = (const char*)sqlite3_column_text(prepared_stmt, 0);
        int earliest = sqlite3_column_int(prepared_stmt, 1);
        int latest = sqlite3_column_int(prepared_stmt, 2);
        float amount = (float)sqlite3_column_double(prepared_stmt, 3);
        total += amount;
        printf(" %5d     %4d      %-20s  %12.2f\n", earliest, latest, payee,
            amount);
    }
    printf("---------------------------------------------------------\n");
    printf("                     TOTAL:                %12.2f\n", total);
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

int compute_date(const char* current_date, int nmonth_ago, date_type type)
{
    char* datep;
    char* tofree;
    tofree = datep = strdup(current_date);
    int year = atoi(strsep(&datep, "-"));
    int month = atoi(strsep(&datep, "-"));
    int day = atoi(strsep(&datep, "-"));
    free(tofree);
    int past_date[3];
    past_date[YEAR] = (month - nmonth_ago <= 0) ? year - 1 : year;
    past_date[MONTH] = (month - nmonth_ago <= 0) ? month + 12 - nmonth_ago
                                                 : month - nmonth_ago;
    past_date[DAY] = day;
    return past_date[type];
}

void print_past_trend(const char* date)
{
    sqlite3* db = open_db(DB);
    int day = compute_date(date, 0, DAY);
    char sql_stmt[1024];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "WITH agg_tb AS ("
        "SELECT strftime('%%Y-%%m', t.date) AS date_y_m, "
        "sum(t.amount) AS sum_amount, c.name AS category "
        "FROM transactions t "
        "LEFT OUTER JOIN categories c ON t.cat_id = c.id "
        "LEFT OUTER JOIN payee p ON t.payee_id = p.id "
        "WHERE t.cat_id != 0 AND t.date BETWEEN '%d-%02d-01' AND '%d-%02d-31' "
        "AND p.name != 'Mortgage' AND t.day > %d "
        "GROUP BY t.cat_id, date_y_m "
        "ORDER By date_y_m, t.cat_id) "
        "SELECT category, avg(sum_amount) AS avg_amount "
        "FROM agg_tb GROUP BY category;",
        compute_date(date, 3, YEAR), compute_date(date, 3, MONTH),
        compute_date(date, 1, YEAR), compute_date(date, 1, MONTH), day);

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }

    printf("\n2. Expense trend after Day-%d in past three months\n\n", day);
    // printf(">> SQL Statement: %s\n\n", sql_stmt);
    printf("--------------------------------\n");
    printf(" CATEGORY          AMOUNT (avg)\n");
    printf("--------------------------------\n");
    float total = 0.0f;
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const char* category
            = (const char*)sqlite3_column_text(prepared_stmt, 0);
        float amount = (float)sqlite3_column_double(prepared_stmt, 1);
        total += amount;
        printf(" %-15s   %12.2f\n", category, amount);
    }
    printf("--------------------------------\n");
    printf(" TOTAL:            %12.2f\n", total);
}

bool is_valid_int(const char* string)
{
    // check if string is number. returns true if string is number or empty.
    // returns false otherwise.
    if (string == NULL) {
        // not a valid string
        return false;
    }
    char* endptr = NULL;
    strtol(string, &endptr, 10);
    if (*endptr == '\0') {
        return true;
    }
    return false;
}

void add_date_filter(char* stmt, char* date_filter)
{
    // Get today's date
    int year = get_today_date(YEAR);
    int month = get_today_date(MONTH);
    int day = get_today_date(DAY);

    char date_buf[128] = "";
    if (*date_filter != '\0') {
        if (strstr(date_filter, ",")) {
            char* from = strsep(&date_filter, ",");
            char* end = strsep(&date_filter, ",");
            int from_year = year;
            int from_month = month;
            int end_year = year;
            int end_month = month;
            int from_day = 1;
            int end_day = 31;

            if (strlen(from) <= 2) {
                sscanf(from, "%d", &from_month);
                sscanf(end, "%d", &end_month);
                snprintf(date_buf, sizeof(date_buf),
                    " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'",
                    from_year, from_month, end_year, end_month);
            } else if (strlen(from) > 2 && strlen(from) <= 5) {
                sscanf(from, "%d-%d", &from_month, &from_day);
                sscanf(end, "%d-%d", &end_month, &end_day);
                snprintf(date_buf, sizeof(date_buf),
                    " WHERE t.date BETWEEN '%4d-%02d-%02d' AND '%4d-%02d-%02d'",
                    from_year, from_month, from_day, end_year, end_month,
                    end_day);
            } else if (strlen(from) > 6 && strlen(from) <= 7) {
                sscanf(from, "%4d-%d", &from_year, &from_month);
                sscanf(end, "%4d-%d", &end_year, &end_month);
                snprintf(date_buf, sizeof(date_buf),
                    " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'",
                    from_year, from_month, end_year, end_month);
            } else {
                sscanf(from, "%4d-%d-%d", &from_year, &from_month, &from_day);
                sscanf(end, "%4d-%d-%d", &end_year, &end_month, &end_day);
                snprintf(date_buf, sizeof(date_buf),
                    " WHERE t.date BETWEEN '%4d-%02d-%02d' AND '%4d-%02d-%02d'",
                    from_year, from_month, from_day, end_year, end_month,
                    end_day);
            }
        } else if (strlen(date_filter) <= 2) {
            sscanf(date_filter, "%d", &month);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else if (strlen(date_filter) == 4) {
            sscanf(date_filter, "%4d", &year);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-01-01' AND '%4d-12-31'", year,
                year);
        } else if (strlen(date_filter) > 4 && strlen(date_filter) <= 7) {
            sscanf(date_filter, "%4d-%d", &year, &month);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else if (strlen(date_filter) > 7 && strlen(date_filter) <= 10) {
            sscanf(date_filter, "%4d-%d-%d", &year, &month, &day);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date = '%4d-%02d-%02d'", year, month, day);
        }
    } else {
        snprintf(date_buf, sizeof(date_buf),
            " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
            month, year, month);
    }
    strlcat(stmt, date_buf, sizeof(stmt) - strlen(stmt) - 1);
}

void add_cat_filter(char* stmt, int cat_id)
{
    if (cat_id >= 0) {
        char cat_buf[32];
        snprintf(cat_buf, sizeof(cat_buf), " AND t.cat_id = %d", cat_id);
        strlcat(stmt, cat_buf, sizeof(stmt) - strlen(stmt) - 1);
    }
}

void add_payee_filter(char* stmt, int payee_id)
{
    if (payee_id >= 0) {
        char payee_buf[32];
        snprintf(
            payee_buf, sizeof(payee_buf), " AND t.payee_id = %d", payee_id);
        strlcat(stmt, payee_buf, sizeof(stmt) - strlen(stmt) - 1);
    }
}

void add_limit_filter(char* stmt, char* limit_filter)
{
    if (*limit_filter) {
        char limit_buf[32];
        snprintf(limit_buf, sizeof(limit_buf), " LIMIT %s;", limit_filter);
        strlcat(stmt, limit_buf, sizeof(stmt) - strlen(stmt) - 1);
    } else {
        strlcat(stmt, ";", sizeof(stmt) - strlen(stmt) - 1);
    }
}

void add_custom_filter(char* stmt, char* filter)
{
    if (*filter) {
        char custom_buf[256];
        snprintf(custom_buf, sizeof(custom_buf), " %s", filter);
        strlcat(stmt, custom_buf, sizeof(stmt) - strlen(stmt) - 1);
    }
}

void compose_sql_stmt(st_query* user_query)
{

    char sql_stmt[1536]
        = "SELECT t.date, t.amount AS amount, t.description AS "
          "description, c.name AS category, t.cat_id, p.name AS "
          "payee FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id "
          "= c.id LEFT OUTER JOIN payee p ON t.payee_id = p.id";

    add_date_filter(sql_stmt, user_query->date);
    add_cat_filter(sql_stmt, user_query->cat_id);
    add_payee_filter(sql_stmt, user_query->payee_id);
    add_custom_filter(sql_stmt, user_query->custom);
    if (!strstr(sql_stmt, "ORDER BY") && !strstr(sql_stmt, "order by")) {
        strlcat(sql_stmt, " ORDER BY t.date",
            sizeof(sql_stmt) - strlen(sql_stmt) - 1);
    }
    add_limit_filter(sql_stmt, user_query->limit);

    strlcpy(user_query->sql_stmt, sql_stmt, sizeof(user_query->sql_stmt) - 1);
}

void print_query(st_query* user_query)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char* sql_stmt = (const char*)user_query->sql_stmt;
    printf("\n> SQL Statement: %s\n\n", sql_stmt);

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }

    float sum_amount = 0.0f;
    printf("DATE             CATEGORY      AMOUNT   DESCRIPTION\n");
    printf("---------------------------------------------------\n");
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const unsigned char* date = sqlite3_column_text(prepared_stmt, 0);
        float amount = (float)sqlite3_column_double(prepared_stmt, 1);
        const unsigned char* category = sqlite3_column_text(prepared_stmt, 3);
        const unsigned char* desc = sqlite3_column_text(prepared_stmt, 2);
        printf("%s %14s  %10.2f   %s\n", date, category, amount, desc);
        sum_amount += amount;
    }
    if (user_query->cat_id > 0) {
        printf("-------------------------------------\n");
        printf("                    TOTAL: %10.2f\n", sum_amount);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void delete_duplicates(void)
{
    sqlite3* db;
    sqlite3_open(DB, &db);

    const char* sql_stmt
        = "DELETE FROM transactions WHERE rowid NOT IN (SELECT min(rowid) "
          "FROM "
          "transactions GROUP BY date, amount, description);";
    sqlite3_exec(db, sql_stmt, NULL, NULL, NULL);
    sqlite3_close(db);
    printf(">> All duplicate records have been successfully removed\n");
}

int get_today_date(date_type type)
{
    char date_buffer[50];
    time_t raw_time = time(NULL);
    struct tm* local_time = localtime(&raw_time);
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", local_time);

    char* date_ptr = date_buffer;
    int today_date[3];
    today_date[YEAR] = atoi(strsep(&date_ptr, "-"));
    today_date[MONTH] = atoi(strsep(&date_ptr, "-"));
    today_date[DAY] = atoi(strsep(&date_ptr, "-"));
    return today_date[type];
}

void category_prompt(void)
{
    char* cat_input;
    const char* prompt = "\n[A]dd categories\n[S]tatistics\n[R]ules\n"
                         "[M]ain menu\n----------------\n> ";
    while (1) {
        if ((cat_input = readline(prompt)) != NULL) {
            if (*cat_input) {
                add_history(cat_input);
            }
            if (strcmp(cat_input, "a") == 0 || strcmp(cat_input, "A") == 0) {
                add_categories();
                free(cat_input);
                break;
            } else if (strcmp(cat_input, "s") == 0
                || strcmp(cat_input, "S") == 0) {
                print_statistics();
                free(cat_input);
                break;
            } else if (strcmp(cat_input, "r") == 0
                || strcmp(cat_input, "R") == 0) {
                view_rules();
                free(cat_input);
                break;
            } else if (strcmp(cat_input, "m") == 0
                || strcmp(cat_input, "M") == 0) {
                free(cat_input);
                break;
            } else if (strcmp(cat_input, "exit") == 0
                || strcmp(cat_input, "quit") == 0) {
                exit(0);
            }
        }
        free(cat_input);
    }
}

static int get_id_callback(void* data, int argc, char** argv, char** azColName)
{
    int* id = (int*)data;
    for (int i = 0; i < argc; i++) {
        if (strcmp(azColName[i], "id") == 0) {
            *id = atoi(argv[i]);
        }
    }
    return 0;
}

static int print_items(sqlite3** db, const char* tablename, int cat_id)
{
    // Add one item to the table given by tablename, and returns the number
    // of items printed or -1 if error occurs
    if (strcmp(tablename, "payee") != 0
        && strcmp(tablename, "categories") != 0) {
        printf("!! Not a valid table name: %s !!\n", tablename);
        return -1;
    }
    char sql_stmt[256];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "SELECT id, name FROM %s WHERE id != 0 ", tablename);

    if (strcmp(tablename, "payee") == 0 || cat_id > 0) {
        char filter_cat[64];
        snprintf(filter_cat, sizeof(filter_cat), "AND cat_id = %d ", cat_id);
        strlcat(sql_stmt, filter_cat, sizeof(sql_stmt) - strlen(sql_stmt) - 1);
    }
    strlcat(sql_stmt, "ORDER BY id;", sizeof(sql_stmt) - strlen(sql_stmt) - 1);
    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(*db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(*db));
        sqlite3_finalize(prepared_stmt);
        return -1;
    }
    int nitems = 0;
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        if (nitems % 3 == 0) {
            printf("\n");
        }
        int id = sqlite3_column_int(prepared_stmt, 0);
        const unsigned char* name = sqlite3_column_text(prepared_stmt, 1);
        printf("[%02d] = %-20s  ", id, name);
        nitems++;
    }
    sqlite3_finalize(prepared_stmt);
    return nitems;
}

static int add_item(sqlite3** db, const char* input, const char* tablename)
{
    // Add item given in input to table given as tablename. Returns id of
    // the added item.
    if (strcmp(tablename, "payee") != 0
        && strcmp(tablename, "categories") != 0) {

        printf("!! Not a valid table name: %s\n", tablename);
        return -1;
    }
    int id = 0;
    if (isdigit(input[0]) && strlen(input) < 3) {
        // One of existing categories chosen
        id = atoi(input);
    } else {
        char sql_stmt[128];
        snprintf(sql_stmt, sizeof(sql_stmt),
            "INSERT INTO %s (name) VALUES (\"%s\");", tablename, input);
        sqlite3_exec(*db, sql_stmt, NULL, NULL, NULL);
        snprintf(sql_stmt, sizeof(sql_stmt),
            "SELECT id FROM %s WHERE name = \"%s\";", tablename, input);
        char* err_msg = NULL;
        sqlite3_exec(*db, sql_stmt, get_id_callback, (void*)&id, &err_msg);
    }
    return id;
}

void add_categories(void)
{
    sqlite3* db = open_db(DB);
    print_list_with_id(&db, 0, -1);
    char* category = readline("\n\nEnter number or new category: ");
    int cat_id = 0;
    if (category != NULL) {
        if (*category) {
            add_history(category);
            cat_id = add_item(&db, category, "categories");
        }
    }
    free(category);

    print_list_with_id(&db, 1, cat_id);
    char* payee = readline("\n\nEnter number or new payee: ");
    int payee_id = 0;
    if (payee != NULL) {
        if (*payee) {
            add_history(payee);
            payee_id = add_item(&db, payee, "payee");
        }
    }
    free(payee);

    char* rule = readline("Rule/Alias: ");
    if (rule != NULL) {
        if (*rule) {
            add_history(rule);
            char sql_rule[128];
            snprintf(sql_rule, sizeof(sql_rule),
                "INSERT INTO rules (rule_pattern, payee_id) VALUES "
                "(\"%%%s%%\", %d);",
                rule, payee_id);
            sqlite3_exec(db, sql_rule, NULL, NULL, NULL);
        }
    }
    free(rule);

    // Update foreign key of payee
    char sql_payee[128];
    snprintf(sql_payee, sizeof(sql_payee),
        "UPDATE payee SET cat_id = %d WHERE id = %d;", cat_id, payee_id);
    sqlite3_exec(db, sql_payee, NULL, NULL, NULL);
    sqlite3_close(db);
}

int print_list_with_id(sqlite3** db, int list_idx, int cat_id)
{
    // list_idx = 0: category
    // list_idx = 1: payee
    const char* tb_names[2] = { "categories", "payee" };
    const char* list_names[2] = { "Category List", "Payee List" };

    printf("\n[[ %s ]]\n", list_names[list_idx]);
    // Fetch all categories in database
    int nitems = print_items(db, tb_names[list_idx], cat_id);
    if (nitems < 0) {
        printf(
            "!! Cannot fetch data from database: %s !!\n", tb_names[list_idx]);
        return -1;
    } else if (nitems == 0) {
        printf("No items on %s", list_names[list_idx]);
    }
    return nitems;
}

record_t* get_statistics(record_t* tr_data, stat_type type)
{
    const char* names[TOTAL_CATEGORIES];
    unique_data(tr_data, get_name, names, TOTAL_CATEGORIES);

    record_t* payee_stats = record_init();
    if (type == MEAN) {
        for (int i = 0; names[i]; i++) {
            add_record(payee_stats, NULL, names[i],
                (float)average_by_name(tr_data, names[i]));
        }
    } else if (type == STDEV) {
        for (int i = 0; names[i]; i++) {
            add_record(payee_stats, NULL, names[i],
                (float)stdev_by_name(tr_data, names[i]));
        }
    }
    return payee_stats;
}

void print_statistics(void)
{
    sqlite3* db = open_db(DB);
    print_list_with_id(&db, 0, -1);
    char* cat_id;
    while (1) {
        cat_id = readline("\n\nCategory: ");
        if (cat_id != NULL) {
            if (*cat_id) {
                add_history(cat_id);
                break;
            }
        }
        free(cat_id);
    }

    int year = get_today_date(YEAR);
    int month = get_today_date(MONTH);

    char sql_stmt[512];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "SELECT c.name, p.name, sum(t.amount) AS amount, strftime('%%Y-%%m', "
        "t.date) AS date_y_m FROM transactions t LEFT OUTER JOIN categories c "
        "ON t.cat_id = c.id LEFT OUTER JOIN payee p ON t.payee_id = p.id WHERE "
        "t.date BETWEEN '2025-01-01' AND '%d-%02d-31' AND c.id = %s GROUP BY "
        "date_y_m, p.id ORDER BY date_y_m, p.id;",
        year, month - 1, cat_id);

    free(cat_id);

    // printf(">> SQL statement: %s\n", sql_stmt);
    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }
    char cat_name[32];
    record_t* tr_data = record_init();
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        // int payee_id = sqlite3_column_int(prepared_stmt, 0);
        const char* category
            = (const char*)sqlite3_column_text(prepared_stmt, 0);
        const char* payee = (const char*)sqlite3_column_text(prepared_stmt, 1);
        float amount = (float)sqlite3_column_double(prepared_stmt, 2);
        const char* date = (const char*)sqlite3_column_text(prepared_stmt, 3);
        add_record(tr_data, date, payee, amount);
        strlcpy(cat_name, category, sizeof(cat_name) - 1);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);

    printf("\n");
    printf("       CATEGORY   ITEMS                       MEAN        STDEV\n");
    printf("---------------------------------------------------------------\n");
    const char* items[TOTAL_CATEGORIES];
    unique_data(tr_data, get_name, items, TOTAL_CATEGORIES);
    record_t* mean_by_item = get_statistics(tr_data, MEAN);
    record_t* stdev_by_item = get_statistics(tr_data, STDEV);
    for (int i = 0; items[i]; i++) {
        printf("%15s   ", cat_name);
        printf("%-20s  ", items[i]);
        printf("%10.2f   ", lookup_amount(mean_by_item, "", items[i]));
        printf("%10.2f\n", lookup_amount(stdev_by_item, "", items[i]));
    }
}

void view_rules(void)
{
    sqlite3* db = open_db(DB);
    print_list_with_id(&db, 0, -1);
    char* cat_id;
    while (1) {
        cat_id = readline("\n\nCategory: ");
        if (cat_id != NULL) {
            if (*cat_id) {
                add_history(cat_id);
                break;
            }
        }
        free(cat_id);
    }
    char sql_stmt[512];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "SELECT p.name, r.rule_pattern FROM rules r JOIN payee p on r.payee_id "
        "= p.id WHERE p.cat_id = %d AND p.id != 0 ORDER BY p.id;",
        atoi(cat_id));

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }
    printf("\n");
    printf("               PAYEE   RULE PATTERN\n");
    printf("-----------------------------------\n");
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const unsigned char* payee = sqlite3_column_text(prepared_stmt, 0);
        const unsigned char* rule_pattern
            = sqlite3_column_text(prepared_stmt, 1);
        printf("%20s   %s\n", payee, rule_pattern);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void update_categories(void)
{
    sqlite3* db = open_db(DB);
    const char* payee_update
        = "UPDATE transactions SET payee_id = "
          "(SELECT r.payee_id FROM rules r WHERE "
          "transactions.description_n LIKE r.rule_pattern "
          "LIMIT 1) WHERE payee_id = 0;"
          "UPDATE transactions SET payee_id = 0 WHERE payee_id IS NULL;";

    const char* cat_update
        = "UPDATE transactions SET cat_id = "
          "(SELECT p.cat_id FROM payee p WHERE "
          "transactions.payee_id = p.id "
          "LIMIT 1) WHERE cat_id = 0; "
          "UPDATE transactions SET cat_id = 0 WHERE cat_id IS NULL;";

    sqlite3_exec(db, payee_update, NULL, NULL, NULL);
    sqlite3_exec(db, cat_update, NULL, NULL, NULL);
    sqlite3_close(db);
    printf(">> Category update completed\n");
}
