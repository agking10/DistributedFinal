DIR="include/property_tree"

if [ -d $DIR ]; then
    echo "Found Boost library"
else
    wget "https://github.com/boostorg/property_tree/archive/d30ff9404bd6af5cc8922a177865e566f4846b19.zip"
    unzip "d30ff9404bd6af5cc8922a177865e566f4846b19.zip" -d include/property_tree
    rm "d30ff9404bd6af5cc8922a177865e566f4846b19.zip"
fi