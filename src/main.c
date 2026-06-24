// DONE: Finish query_budget function
// DONE: Check duplicate items and retain only one and discard the other
// DONE: Work on import logic flow, so that I can feed the raw statement files
// as is (apply awk parser on the way)
// WORKING: Add more categories
// TODO: Switch from fgets() to readline()
// TODO: Clean up all functions and codes
// TODO: Split codes into multiple files
// TODO: When updating data, report the number of items affected (category,
// duplicate)
// TODO: Allow for category input when query
// TODO: Add monthly/yearly summary report
// TODO: Find recurring payments and notify what payments are left

#include <ctype.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void execute(const char* input);
bool check_file(const char* buf);
void import_csv(char* filename);
void query_budget(void);
void sql_query(const char* database, const char* user_filter);
void delete_duplicates(const char* database);
void update_categories(const char* datebase);
void date_today(char* date_buffer);
void import_bank_stmt(void);
void parse_txt(char* filename);
void exec_categories(void);
void add_categories(void);
void view_categories(void);
void view_rules(void);
static int get_id_callback(void* data, int argc, char** argv, char** azColName);
static int add_item(sqlite3** db, const char* input, const char* tablename);
static int print_items(sqlite3** db, const char* tablename);

int main(void)
{
    char input[1024];
    while (1) {
        printf("\n[I]mport statements  [R]etrieve/reports  [C]ategories  "
               "[U]pdate database\n");
        printf("budget> ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            input[strlen(input) - 1] = '\0'; // remove '\n'
            if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0)
                break;
            else
                execute(input);
        }
    }
    return 0;
}

void execute(const char* input)
{
    // char buf[1024];
    if (!strcmp(input, "i") || !strcmp(input, "I")) {
        import_bank_stmt();
    } else if (!strcmp(input, "r") || !strcmp(input, "R")) {
        query_budget();
    } else if (!strcmp(input, "u") || !strcmp(input, "U")) {
        // refreshing tables means that it deletes all duplicate rows and
        // categorize transactions that have not yet been categorized.
        delete_duplicates("budget.db");
        update_categories("budget.db");
    } else if (!strcmp(input, "c") || !strcmp(input, "C")) {
        exec_categories();
    }
}

void import_bank_stmt(void)
{
    char filename[128];
    while (1) {
        printf("\nEnter file name or  [M]ain menu\n");
        printf("> ");
        if (fgets(filename, sizeof(filename), stdin) != NULL) {
            filename[strlen(filename) - 1] = '\0'; // remove '\n'
            if (!strcmp(filename, "m") || !strcmp(filename, "M")) {
                break;
            } else if (check_file(filename)) {
                import_csv(filename);
                printf("\n>> \"%s\" successfully imported\n", filename);
                break;
            }
        }
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
    if (sqlite3_open_v2("budget.db", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
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
    char* linep;
    int row = 0;
    sqlite3_stmt* out_stmt;

    sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);

    const char* in_stmt = "INSERT INTO transactions (date, description, "
                          "description_n, amount, payee_id, cat_id)"
                          "VALUES (?, ?, ?, ?, ?, ?);";

    if (sqlite3_prepare_v2(db, in_stmt, -1, &out_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare error: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    while (fgets(line, sizeof(line), fp)) {
        row++;
        linep = line;
        char* date = strsep(&linep, ",");
        char* desc = strsep(&linep, ",");
        char* desc_n = strsep(&linep, ",");
        char* amount = strsep(&linep, ",");
        char* payee_id = strsep(&linep, ",");
        char* cat_id = strsep(&linep, "\n");

        sqlite3_bind_text(out_stmt, 1, date, -1, NULL);
        sqlite3_bind_text(out_stmt, 2, desc, -1, NULL);
        sqlite3_bind_text(out_stmt, 3, desc_n, -1, NULL);
        sqlite3_bind_double(out_stmt, 4, atof(amount));
        sqlite3_bind_int(out_stmt, 5, atoi(payee_id));
        sqlite3_bind_int(out_stmt, 6, atoi(cat_id));

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

void query_budget(void)
{
    char* input = NULL;
    size_t linecap = 0;

    printf("Filters: ");
    ssize_t len = getline(&input, &linecap, stdin);
    if (len < 0) {
        fprintf(stderr, "getline: error or EOF\n");
        free(input);
        exit(1);
    }
    // remove '\n' from input
    input[strlen(input) - 1] = '\0';

    sql_query("budget.db", input);

    free(input);
}

void sql_query(const char* database, const char* user_filter)
{
    sqlite3* db;
    sqlite3_open(database, &db);

    // Get today's date
    char date_buffer[50];
    date_today(date_buffer);

    // get current year for deafult value
    char* datep = date_buffer;
    int year = atoi(strsep(&datep, "-"));
    int month;

    char sql_stmt[1024]
        = "SELECT t.date AS date, t.amount AS amount, t.description AS "
          "description, c.name AS category "
          "FROM transactions t LEFT OUTER JOIN categories c ON t.cat_id = c.id";
    if (strlen(user_filter) != 0) {
        char stmt_add[512] = "";
        if (strlen(user_filter) <= 3) {
            sscanf(user_filter, "%d", &month);
            snprintf(stmt_add, sizeof(stmt_add),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else if (sscanf(user_filter, "%4d-%02d", &year, &month)) {
            snprintf(stmt_add, sizeof(stmt_add),
                " WHERE t.date BETWEEN '%4d-%02d-01' AND '%4d-%02d-31'", year,
                month, year, month);
        } else {
            snprintf(stmt_add, sizeof(stmt_add), " WHERE %s", user_filter);
        }
        strlcat(sql_stmt, stmt_add, sizeof(sql_stmt) - strlen(sql_stmt) - 1);
        if (!strstr(user_filter, "ORDER BY")) {
            strlcat(sql_stmt, " ORDER BY t.date",
                sizeof(sql_stmt) - strlen(sql_stmt) - 1);
        }
    }

    // Take input from user for number of items for display
    char limit[10];
    printf("Number of Items: ");
    if (fgets(limit, sizeof(limit), stdin) != NULL) {
        limit[strlen(limit) - 1] = '\0';
        if (strlen(limit) != 0) {
            char add_limit[20];
            snprintf(add_limit, sizeof(add_limit), " LIMIT %s", limit);
            strlcat(
                sql_stmt, add_limit, sizeof(sql_stmt) - strlen(sql_stmt) - 1);
        }
    }

    strlcat(sql_stmt, ";", sizeof(sql_stmt) - strlen(sql_stmt) - 1);

    printf("\n> SQL Statement: %s\n\n", sql_stmt);

    sqlite3_stmt* out_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &out_stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(out_stmt);
        sqlite3_close(db);
        exit(1);
    }

    printf("DATE            AMOUNT   CATEGORY         DESCRIPTION\n");
    printf("-----------------------------------------------------\n");
    while (sqlite3_step(out_stmt) == SQLITE_ROW) {
        const unsigned char* date = sqlite3_column_text(out_stmt, 0);
        float amount = (float)sqlite3_column_double(out_stmt, 1);
        const unsigned char* category = sqlite3_column_text(out_stmt, 3);
        const unsigned char* desc = sqlite3_column_text(out_stmt, 2);
        printf("%s  %10.2f   %-10s   %s\n", date, amount, category, desc);
    }
    sqlite3_finalize(out_stmt);
    sqlite3_close(db);
}

void delete_duplicates(const char* database)
{
    sqlite3* db;
    sqlite3_open(database, &db);

    const char* sql_stmt
        = "DELETE FROM transactions WHERE rowid NOT IN (SELECT min(rowid) FROM "
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
    char cat_input[8];

    while (1) {
        printf(
            "\n[A]dd categories   [V]iew categories   [R]ules   [M]ain menu\n");
        printf("> ");

        if (fgets(cat_input, sizeof(cat_input), stdin) != NULL) {
            cat_input[strlen(cat_input) - 1] = '\0';
            if (strcmp(cat_input, "a") == 0 || strcmp(cat_input, "A") == 0) {
                add_categories();
            } else if (strcmp(cat_input, "v") == 0
                || strcmp(cat_input, "V") == 0) {
                view_categories();
            } else if (strcmp(cat_input, "r") == 0
                || strcmp(cat_input, "R") == 0) {
                view_rules();
            } else if (strcmp(cat_input, "m") == 0
                || strcmp(cat_input, "M") == 0) {
                break;
            } else if (strcmp(cat_input, "exit") == 0
                || strcmp(cat_input, "quit") == 0) {
                exit(0);
            }
        }
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
    // Add one item to the table given by tablename, and returns the number of
    // items printed or -1 if error occurs
    if (strcmp(tablename, "payee") != 0
        && strcmp(tablename, "categories") != 0) {
        printf("!! Not a valid table name: %s\n", tablename);
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
        printf("[%d] = %-20s", id, name);
        nitems++;
    }
    sqlite3_finalize(prepared_stmt);
    return nitems;
}

static int add_item(sqlite3** db, const char* input, const char* tablename)
{
    // Add item given in input to table given as tablename. Returns id of the
    // added item.
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
    if (sqlite3_open_v2("budget.db", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    char payee[64];
    char rule[64];
    char category[64];

    printf("\n[[ Payee List ]]\n");
    // Fetch all payee in database
    int payee_items = print_items(&db, "payee");
    if (payee_items < 0) {
        printf("!! Cannot fetch data from database: payee\n");
        return;
    } else if (payee_items == 0) {
        printf("No items on payee list");
    }

    printf("\n\nEnter number or new payee: ");
    int payee_id = 0;
    if (fgets(payee, sizeof(payee), stdin) == NULL) {
        printf("Not a valid response for payee\n");
        return;
    } else {
        payee[strlen(payee) - 1] = '\0';
        payee_id = add_item(&db, payee, "payee");
    }

    printf("Rule/Alias: ");
    if (fgets(rule, sizeof(rule), stdin) == NULL) {
        printf("!! Not a valid response for rules/aliases\n");
        return;
    } else {
        rule[strlen(rule) - 1] = '\0';
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
    printf("\n\nEnter number or new category: ");

    int cat_id = 0;
    if (fgets(category, sizeof(category), stdin) == NULL) {
        printf("!! Not a valid response for categories\n");
        return;
    } else {
        category[strlen(category) - 1] = '\0';
        cat_id = add_item(&db, category, "categories");
    }

    // Add rules only if it's not empty
    if (strlen(rule) > 0) {
        char sql_rule[128];
        snprintf(sql_rule, sizeof(sql_rule),
            "INSERT INTO rules (rule_pattern, payee_id) VALUES (\"%%%s%%\", "
            "%d);",
            rule, payee_id);
        sqlite3_exec(db, sql_rule, NULL, NULL, NULL);
    }

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
    if (sqlite3_open_v2("budget.db", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    const char* sql_stmt
        = "SELECT c.name, p.name FROM categories c "
          "JOIN payee p ON c.id = p.cat_id WHERE c.id != 0 ORDER BY c.id;";

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }
    printf("\n");
    printf("CATEGORY           PAYEE\n");
    printf("------------------------\n");
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const unsigned char* cat = sqlite3_column_text(prepared_stmt, 0);
        const unsigned char* payee = sqlite3_column_text(prepared_stmt, 1);
        printf("%-16s   %-16s\n", cat, payee);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void view_rules(void)
{
    sqlite3* db;
    if (sqlite3_open_v2("budget.db", &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "db open error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    const char* sql_stmt
        = "SELECT p.name, r.rule_pattern FROM payee p "
          "JOIN rules r ON p.id = r.payee_id WHERE p.id != 0 ORDER BY p.id;";

    sqlite3_stmt* prepared_stmt;
    if (sqlite3_prepare_v2(db, sql_stmt, -1, &prepared_stmt, NULL)
        != SQLITE_OK) {
        fprintf(stderr, "sqlite3_prepare: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(prepared_stmt);
        sqlite3_close(db);
        exit(1);
    }
    printf("\n");
    printf("PAYEE              RULE PATTERN\n");
    printf("-------------------------------\n");
    while (sqlite3_step(prepared_stmt) == SQLITE_ROW) {
        const unsigned char* payee = sqlite3_column_text(prepared_stmt, 0);
        const unsigned char* rule_pattern
            = sqlite3_column_text(prepared_stmt, 1);
        printf("%-16s   %s\n", payee, rule_pattern);
    }
    sqlite3_finalize(prepared_stmt);
    sqlite3_close(db);
}

void update_categories(const char* database)
{
    sqlite3* db;
    if (sqlite3_open_v2(
            database, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
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