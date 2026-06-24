#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    // check the number of arguments
    if (argc != 2) {
        fprintf(stderr, "usage: %s <database>\n", argv[0]);
        exit(1);
    }
    // Create or open database
    sqlite3* db;
    if (sqlite3_open_v2(
            argv[1], &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
        fprintf(stderr, "%s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    // Create a transaction table
    char* tb_transactions
        = "CREATE TABLE IF NOT EXISTS transactions ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
          "date TEXT NOT NULL, "
          "description TEXT NOT NULL, "
          "description_n TEXT NOT NULL, "
          "amount REAL NOT NULL, "
          "payee_id INTEGER DEFAULT 0, "
          "cat_id INTEGER DEFAULT 0, "
          "FOREIGN KEY (payee_id) REFERENCES payee(id) ON UPDATE "
          "CASCADE ON DELETE SET DEFAULT, "
          "FOREIGN KEY (cat_id) REFERENCES categories(id) ON UPDATE "
          "CASCADE ON DELETE SET DEFAULT);";
    sqlite3_exec(db, tb_transactions, NULL, NULL, NULL);
    printf("Table \"transactions\" successfully created\n");

    // Create a rules table
    char* tb_rules = "CREATE TABLE IF NOT EXISTS rules(id INTEGER PRIMARY KEY "
                     "AUTOINCREMENT, rule_pattern TEXT NOT NULL, "
                     "payee_id INTEGER DEFAULT 0, "
                     "FOREIGN KEY (payee_id) REFERENCES payee(id) ON UPDATE "
                     "CASCADE ON DELETE SET DEFAULT);";
    sqlite3_exec(db, tb_rules, NULL, NULL, NULL);
    printf("Table \"rules\" successfully created\n");

    // Create a payee table
    char* tb_payee = "CREATE TABLE IF NOT EXISTS payee (id INTEGER PRIMARY KEY "
                     "AUTOINCREMENT, name TEXT NOT NULL UNIQUE, "
                     "cat_id INTEGER DEFAULT 0, "
                     "FOREIGN KEY (cat_id) REFERENCES categories(id) ON UPDATE "
                     "CASCADE ON DELETE SET DEFAULT);";
    sqlite3_exec(db, tb_payee, NULL, NULL, NULL);
    printf("Table \"payee\" successfully created\n");

    // Create a categories table
    char* tb_categories
        = "CREATE TABLE IF NOT EXISTS categories(id INTEGER PRIMARY KEY "
          "AUTOINCREMENT, name TEXT NOT NULL UNIQUE);";
    sqlite3_exec(db, tb_categories, NULL, NULL, NULL);
    printf("Table \"categories\" successfully created\n");

    sqlite3_close(db);
    return 0;
}
