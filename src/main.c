// DONE: Finish query_budget function
// DONE: Check duplicate items and retain only one and discard the other
// DONE: Work on import logic flow, so that I can feed the raw statement files
// as is (apply awk parser on the way)
// DONE: git the code and upload it to github
// DONE: refactor the way it gets query from user
// DONE: Switch from fgets() to readline()
// WORKING: Add more categories and rules
// WORKING: Add monthly/yearly summary report
// TODO: Clean up all functions and codes
// TODO: Split codes into multiple files
// TODO: When updating data, report the number of items affected (category,
// duplicate)
// TODO: Find recurring payments and notify what payments are left and when is
// due

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
#define TOTAL_SUMMARY_MONTHS 12
#define TOTAL_SUMMARY_CATEGORIES 10

typedef struct {
    char sql_stmt[1024];
    char custom[256];
    char date[64];
    char category[8];
    char limit[8];
} st_query;

bool validate_input(const char* input, const char* pattern);
void execute(const char* input);
bool check_file(const char* buf);
void import_csv(char* filename);
void retrieve_or_report(void);
void query_budget(void);
void summary_budget(void);
void add_date_filter(char* dst, char* source);
void add_cat_filter(char* dst, char* source);
void add_limit_filter(char* dst, char* source);
void compose_sql_stmt(st_query* user_query);
void compose_summary_stmt(st_query* user_stmt);
void print_query(st_query* user_query);
void print_summary(st_query* user_stmt);
void delete_duplicates(void);
void update_categories(void);
void date_today(char* date_buffer);
void import_bank_stmt(void);
void parse_txt(char* filename);
void exec_categories(void);
void add_categories(void);
void view_categories(void);
void view_rules(void);
bool is_valid_int(const char* string);
static int get_id_callback(void* data, int argc, char** argv, char** azColName);
static int add_item(sqlite3** db, const char* input, const char* tablename);
static int print_items(sqlite3** db, const char* tablename);

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
            }
            if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
                free(input);
                break;
            } else {
                execute(input);
            }
            free(input);
        }
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

void execute(const char* input)
{
    // char buf[1024];
    if (strcmp(input, "i") == 0 || strcmp(input, "I") == 0) {
        import_bank_stmt();
    } else if (strcmp(input, "r") == 0 || strcmp(input, "R") == 0) {
        retrieve_or_report();
    } else if (strcmp(input, "u") == 0 || strcmp(input, "U") == 0) {
        // refreshing tables means that it deletes all duplicate rows and
        // categorize transactions that have not yet been categorized.
        printf("\n");
        delete_duplicates();
        update_categories();
    } else if (strcmp(input, "c") == 0 || strcmp(input, "C") == 0) {
        exec_categories();
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
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
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
                summary_budget();
                free(input);
                break;
            } else if (strcmp(input, "f") == 0 || strcmp(input, "F") == 0) {
                printf("Forecast report chosen!\n");
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

void query_budget(void)
{
    st_query user_query;
    const char* prompts[] = { "Date: ", "\n\nCategory (number): ",
        "Number of items: ", "Other filters: ", NULL };
    for (int i = 0; prompts[i]; i++) {
        if (i == 1) {
            sqlite3* db;
            if (sqlite3_open_v2(DB, &db,
                    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
                fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
                sqlite3_close(db);
                exit(1);
            }
            printf("\n[[ Category List ]]\n");
            int cat_items = print_items(&db, "categories");
            if (cat_items < 0) {
                printf("!! Cannot fetch data from database: categories !!\n");
                return;
            } else if (cat_items == 0) {
                printf("No items on category list");
            }
        }
        char* query = readline(prompts[i]);
        if (query != NULL) {
            if (*query) {
                add_history(query);
            }
            if (i == 0) {
                if (validate_input(query, "^[0-9-]{1,10}[,]?[ ]*[0-9-]{0,10}$")
                    || *query == '\0') {
                    strlcpy(
                        user_query.date, query, sizeof(user_query.date) - 1);
                } else {
                    printf("\n!! Not a valid input for date !!\n");
                    free(query);
                    return;
                }
            } else if (i == 1) {
                if (!is_valid_int(query)) {
                    printf("!! Not a valid number !!\n");
                    free(query);
                    return;
                }
                strlcpy(user_query.category, query,
                    sizeof(user_query.category) - 1);
            } else if (i == 2) {
                if (!is_valid_int(query)) {
                    printf("!! Not a valid number !!\n");
                    free(query);
                    return;
                }
                strlcpy(user_query.limit, query, sizeof(user_query.limit) - 1);
            } else if (i == 3) {
                strlcpy(
                    user_query.custom, query, sizeof(user_query.custom) - 1);
            }
        }
        free(query);
    }
    compose_sql_stmt(&user_query);
    print_query(&user_query);
}

void summary_budget(void)
{
    st_query user_stmt;
    while (1) {
        char* input = readline("\nEnter month or year for summary: ");
        if (input != NULL) {
            if (*input) {
                add_history(input);
                if (validate_input(input, "^[0-9]{4}-[0-9]{1,2}$")
                    || validate_input(input, "^[0-9]{4}$")
                    || validate_input(input, "^[0-9]{1,2}$")) {
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
          "sum(t.amount) AS amount, c.name as category "
          "FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id =c.id "
          "LEFT OUTER JOIN payee p ON t.payee_id = p.id ";
    add_date_filter(sql_stmt, user_stmt->date);
    char* more_stmt = " AND t.cat_id != 0 AND p.name NOT LIKE 'mortgage' GROUP "
                      "BY date_y_m, t.cat_id ORDER BY date_y_m, category;";
    strlcat(sql_stmt, more_stmt, sizeof(sql_stmt) - strlen(sql_stmt) - 1);

    strlcpy(user_stmt->sql_stmt, sql_stmt, sizeof(user_stmt->sql_stmt) - 1);
}

void unique_months(record_t* tr_data, const char* months[], int n_months)
{
    int idx = 0;
    for (record_t* recordp = tr_data; recordp; recordp = next_record(recordp)) {
        if (get_id(recordp) == 0) {
            continue;
        }
        const char* month = get_date(recordp);
        bool exist = false;
        for (int i = 0; i < idx; i++) {
            if (strcmp(month, months[i]) == 0) {
                exist = true;
                break;
            }
        }
        if (!exist) {
            if (idx < n_months - 1) {
                months[idx] = month;
                idx++;
            } else {
                break;
            }
        }
    }
    months[idx] = NULL;
}

void unique_categories(record_t* tr_data, const char* cats[], int n_cats)
{
    int idx = 0;
    for (record_t* recordp = tr_data; recordp != NULL;
        recordp = next_record(recordp)) {
        if (get_id(recordp) == 0) {
            continue;
        }
        const char* cat = get_name(recordp);
        bool exist = false;
        for (int i = 0; i < idx; i++) {
            if (strcmp(cat, cats[i]) == 0) {
                exist = true;
                break;
            }
        }
        if (!exist) {
            if (idx < n_cats - 1) {
                cats[idx] = cat;
                idx++;
            } else {
                break;
            }
        }
    }
    cats[idx] = NULL;
}

void print_summary(st_query* user_stmt)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
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
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const char* date = (const char*)sqlite3_column_text(prepared_stmt, 0);
        float amount = (float)sqlite3_column_double(prepared_stmt, 1);
        const char* category
            = (const char*)sqlite3_column_text(prepared_stmt, 2);
        add_record(tr_data, date, category, amount);
    }

    // Get unique months and categories
    const char* months[TOTAL_SUMMARY_MONTHS + 1];
    const char* cats[TOTAL_SUMMARY_CATEGORIES + 1];
    unique_months(tr_data, months, TOTAL_SUMMARY_MONTHS + 1);
    unique_categories(tr_data, cats, TOTAL_SUMMARY_CATEGORIES + 1);

    // Print result
    int n_columns = 6;
    bool lt_two_rows = false;
    for (int row = 0; row < TOTAL_SUMMARY_MONTHS / n_columns; row++) {
        char tb_ruler[512] = "-";
        int row_idx = row * n_columns;
        printf("       CATEGORY");
        strlcat(tb_ruler, "---------------",
            sizeof(tb_ruler) - strlen(tb_ruler) - 1);
        for (int i = 0; i < n_columns; i++) {
            if (!months[i]) {
                lt_two_rows = true;
                break;
            }
            printf("   %10s", months[row_idx + i]);
            strlcat(tb_ruler, "-------------",
                sizeof(tb_ruler) - strlen(tb_ruler) - 1);
        }
        printf("\n%s\n", tb_ruler);
        for (int i = 0; cats[i]; i++) {
            printf("%15s", cats[i]);
            for (int j = 0; months[j] && j < n_columns; j++) {
                printf("   %10.2f",
                    lookup_amount(tr_data, months[row_idx + j], cats[i]));
            }
            printf("\n");
        }
        printf("%s\n", tb_ruler);
        if (lt_two_rows) {
            break;
        }
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
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

void add_date_filter(char* dst, char* source)
{
    // Get today's date
    char date_buffer[50];
    date_today(date_buffer);

    // get current year and current month for deafult value
    char* datep = date_buffer;
    int year = atoi(strsep(&datep, "-"));
    int month = atoi(strsep(&datep, "-"));
    int day = atoi(strsep(&datep, "\n"));

    char date_buf[128] = "";
    if (*source != '\0') {
        if (strstr(source, ",")) {
            char* from = strsep(&source, ",");
            char* end = strsep(&source, ",");
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
        } else if (strlen(source) <= 2) {
            sscanf(source, "%d", &month);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else if (strlen(source) == 4) {
            sscanf(source, "%4d", &year);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-01-01' AND '%4d-12-31'", year,
                year);
        } else if (strlen(source) > 4 && strlen(source) <= 7) {
            sscanf(source, "%4d-%d", &year, &month);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else if (strlen(source) > 7 && strlen(source) <= 10) {
            sscanf(source, "%4d-%d-%d", &year, &month, &day);
            snprintf(date_buf, sizeof(date_buf),
                " WHERE t.date = '%4d-%02d-%02d'", year, month, day);
        }
    } else {
        snprintf(date_buf, sizeof(date_buf),
            " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
            month, year, month);
    }
    strlcat(dst, date_buf, sizeof(dst) - strlen(dst) - 1);
}

void add_cat_filter(char* dst, char* source)
{
    if (*source) {
        char cat_buf[32];
        snprintf(cat_buf, sizeof(cat_buf), " AND t.cat_id = %s", source);
        strlcat(dst, cat_buf, sizeof(dst) - strlen(dst) - 1);
    }
}

void add_limit_filter(char* dst, char* source)
{
    if (*source) {
        char limit_buf[32];
        snprintf(limit_buf, sizeof(limit_buf), " LIMIT %s;", source);
        strlcat(dst, limit_buf, sizeof(dst) - strlen(dst) - 1);
    } else {
        strlcat(dst, ";", sizeof(dst) - strlen(dst) - 1);
    }
}

void add_custom_filter(char* dst, char* source)
{
    if (*source) {
        char custom_buf[256];
        snprintf(custom_buf, sizeof(custom_buf), " %s", source);
        strlcat(dst, custom_buf, sizeof(dst) - strlen(dst) - 1);
    }
}

void compose_sql_stmt(st_query* user_query)
{

    char sql_stmt[1024]
        = "SELECT t.date, t.amount AS amount, t.description AS "
          "description, c.name AS category, t.cat_id, p.name AS "
          "payee FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id "
          "= c.id LEFT OUTER JOIN payee p ON t.payee_id = p.id";

    // char sql_stmt[1024]
    //     = "SELECT strftime( % Y - % m, t.date) AS date_y_m, "
    //       "sum(t.amount) AS sum_amount, c.name as categories "
    //       "FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id =
    //       c.id " "LEFT OUTER JOIN payee p ON t.payee_id = p.id " "WHERE
    //       t.cat_id != 0 AND date_y_m != '2024-12' " "AND date_y_m !=
    //       '2026-05' " "AND p.name NOT LIKE 'mortgage' AND t.day > 20 "
    //       "AND t.date BETWEEN '2026-02-01' AND '2026-04-30' "
    //       "GROUP BY t.cat_id, date_y_m ORDER BY date_y_m, categories;";
    add_date_filter(sql_stmt, user_query->date);
    add_cat_filter(sql_stmt, user_query->category);
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

    float sum_amount = 0.0;
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
    if (*user_query->category) {
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

void date_today(char* date_buffer)
{
    time_t raw_time = time(NULL);
    struct tm* local_time = localtime(&raw_time);
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", local_time);
}

void exec_categories(void)
{
    char* cat_input;
    const char* prompt = "\n[A]dd categories\n[V]iew categories\n[R]ules\n"
                         "[M]ain menu\n-----------------\n> ";
    while (1) {
        if ((cat_input = readline(prompt)) != NULL) {
            if (*cat_input) {
                add_history(cat_input);
            }
            if (strcmp(cat_input, "a") == 0 || strcmp(cat_input, "A") == 0) {
                add_categories();
                free(cat_input);
                break;
            } else if (strcmp(cat_input, "v") == 0
                || strcmp(cat_input, "V") == 0) {
                view_categories();
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

static int print_items(sqlite3** db, const char* tablename)
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
        "SELECT id, name FROM %s WHERE id != 0 ORDER BY id;", tablename);

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
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    printf("\n[[ Payee List ]]\n");
    // Fetch all payee in database
    int payee_items = print_items(&db, "payee");
    if (payee_items < 0) {
        printf("!! Cannot fetch data from database: payee\n");
        return;
    } else if (payee_items == 0) {
        printf("No items on payee list");
    }

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

    printf("\n[[ Category List ]]\n");
    // Fetch all categories in database
    int cat_items = print_items(&db, "categories");
    if (cat_items < 0) {
        printf("!! Cannot fetch data from database: categories\n");
        return;
    } else if (cat_items == 0) {
        printf("No items on category list");
    }

    char* category = readline("\n\nEnter number or new category: ");
    int cat_id = 0;
    if (category != NULL) {
        if (*category) {
            add_history(category);
            cat_id = add_item(&db, category, "categories");
        }
    }
    free(category);

    // Update foreign key of payee
    char sql_payee[128];
    snprintf(sql_payee, sizeof(sql_payee),
        "UPDATE payee SET cat_id = %d WHERE id = %d;", cat_id, payee_id);
    sqlite3_exec(db, sql_payee, NULL, NULL, NULL);
    sqlite3_close(db);
}

void view_categories(void)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    printf("\n[[ Category List ]]\n");
    // Fetch all categories in database
    int cat_items = print_items(&db, "categories");
    if (cat_items < 0) {
        printf("!! Cannot fetch data from database: categories\n");
        return;
    } else if (cat_items == 0) {
        printf("No items on category list");
    }
    char* cat_id;
    while (1) {
        cat_id = readline("\n\nCategory: ");
        if (cat_id != NULL) {
            if (*cat_id) {
                add_history(cat_id);
                free(cat_id);
                break;
            }
        }
        free(cat_id);
    }

    char sql_stmt[512];
    snprintf(sql_stmt, sizeof(sql_stmt),
        "SELECT p.id, c.name, p.name FROM categories c "
        "JOIN payee p ON c.id = p.cat_id WHERE c.id = %s ORDER BY p.id;",
        cat_id);

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }
    printf("\n");
    printf("      CATEGORY   ID   PAYEE\n");
    printf("      ---------------------\n");
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        int payee_id = sqlite3_column_int(prepared_stmt, 0);
        const unsigned char* category = sqlite3_column_text(prepared_stmt, 1);
        const unsigned char* payee = sqlite3_column_text(prepared_stmt, 2);
        printf("%14s   %2d   %-16s\n", category, payee_id, payee);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void view_rules(void)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    const char* sql_stmt = "SELECT p.name, r.rule_pattern FROM payee p "
                           "JOIN rules r ON p.id = r.payee_id WHERE p.id "
                           "!= 0 ORDER BY p.id;";

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
    printf("               --------------------\n");
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
    sqlite3* db;
    if (sqlite3_open_v2(
            DB, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char* payee_update
        = "UPDATE transactions SET payee_id = "
          "(SELECT r.payee_id FROM rules r WHERE "
          "transactions.description_n LIKE r.rule_pattern "
          "LIMIT 1) WHERE payee_id = 0;"
          "UPDATE transactions SET payee_id = 0 WHERE payee_id IS NULL";

    const char* cat_update
        = "UPDATE transactions SET cat_id = "
          "(SELECT p.cat_id FROM payee p WHERE "
          "transactions.payee_id = p.id "
          "LIMIT 1) WHERE cat_id = 0; "
          "UPDATE transactions SET cat_id = 0 WHERE cat_id IS NULL";

    sqlite3_exec(db, payee_update, NULL, NULL, NULL);
    sqlite3_exec(db, cat_update, NULL, NULL, NULL);
    sqlite3_close(db);
    printf(">> Category update completed\n");
}
