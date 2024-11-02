




fmt: main.c
	clang-format main.c -i



INCLUDE = -Iusr/local/include/
LIB = -Iusr/local/lib -lglfw -lvulkan
debug: main.c
	clang main.c -o main $(INCLUDE) $(LIB) -Wall -ferror-limit=0

release: main.c
	clang main.c -o main $(INCLUDE) $(LIB) -Wall -ferror-limit=0 -Ofast- -march=native