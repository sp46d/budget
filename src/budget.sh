# if [ -s stmt.csv ]; then
#     rm -f -- stmt.csv 
# fi

for filename in "$@"
do
    awk -f parser.awk "$filename"
done
