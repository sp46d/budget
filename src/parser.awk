BEGIN {
    FS = "[ \t]{2,}"
    # OFS = ","  # No more needed. the results are now printed using printf
}

# skip header
NF != 4 { next }

# Include only the rows that have transaction records
$1 ~ /^[0-9]{2}\/[0-9]{2}\/[0-9]{4}/ {
    # Change date into ISO format
    split($1, tmp, "/")
    $1 = sprintf("%4d-%02d-%02d", tmp[3], tmp[1], tmp[2])
    # remove "," from description, amount and balance fields
    sub(",", "", $2)
    sub(",", "", $3)
    sub(",", "", $4)
    # create a normalized description column ($5)
    $5 = $2
    # normalize by removing all noisy tokens
    gsub(/[ .,;:\/#*-]/, "", $5)
    $5 = tolower($5)
    # create category and merchant columns
    $6 = 0
    $7 = 0
    # output processed records
    # date, year, month, day, description, description_n, amount, payee_id, cat_id
    printf("%s,%d,%d,%d,%s,%s,%0.2f,%d,%d\n",
        $1, tmp[3], tmp[1], tmp[2], $2, $5, $3, $6, $7)
}

# Create an array of categories out of list files
# FILENAME != ARGV[ARGC-1] {
#     category = FILENAME
#     sub(/\.list$/, "", category)
#     items[tolower($2)] = category
#     next
# }


# $1 ~ /^[0-9]{2}\/[0-9]{2}\/[0-9]{4}/ {
    # Remove "," from amount and balance columns for better computation
    # sub(",", "", $3)
    # sub(",", "", $4)
    # # Extract actual transaction date
    # split($1, date, "/")
    # if (match($2, /[0-9]{2}\/[0-9]{2}/)) {
    #     $5 = substr($2, RSTART, RLENGTH) "/" date[3]
    # } else {
    #     $5 = $1
    # }
    # Store dates in ISO format
    # split($1, tmp, "/")
    # $1 = sprintf("%4d-%02d-%02d", tmp[3], tmp[1], tmp[2])
    # Categorize each transaction and store it in $5
    # matched = 0
    # desc_lower = tolower($2)
    # for (key in items) {
    #     if (desc_lower ~ key) {
    #         $5 = items[key]
    #         matched = 1
    #         break
    #     }
    # }
    # if (!matched) $5 = "etc"
    # printf("%s %9s %8s  %s\n", $1, $3, $5, $2)
# }


# $5 ~ "grocery" {
#     g_sum += $3
#     # printf("%s %9s %8s  %s\n", $1, $3, $5, $2)
# }
# $5 ~ "fixed" {
#     f_sum += $3
#     # printf("%s %9s %8s  %s\n", $1, $3, $5, $2)
# }

# END {
    # print "Grocery:", g_sum
    # print "Fixed  :", f_sum
# }
        

