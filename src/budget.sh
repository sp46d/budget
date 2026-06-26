for filename in "$@"
do
    awk -f parser.awk "$filename"
done | sort 
