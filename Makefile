default: all
all: clean
	g++ -Wall -Wextra -Weffc++ -Werror -fno-exceptions -fno-rtti -O3 -o bplustree bplustree.cpp
	strip bplustree
clean:
	rm -rf bplustree_dbg bplustree
dbug: clean
	g++ -Wall -Wextra -Weffc++ -Werror -fno-exceptions -fno-rtti -O -g -DEBUG -o bplustree_dbg bplustree.cpp
