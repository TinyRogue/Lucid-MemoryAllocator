cc=clang
files=*.c
output_filename=out
arguments=
flags= -std=c11 -Wall -Wextra -pedantic
valgrind_flags= --leak-check=full -s
valgrind_log= --log-file=logs.txt
post_flags=
output= -o $(output_filename)


all:
	@clear && $(cc) $(flags) $(files) $(output) $(post_flags) && ./$(output_filename) $(arguments) && rm $(output_filename)
d:
	@clear && $(cc) $(flags) -g $(files) $(output) $(post_flags) && sudo gdb ./$(output_filename) $(arguments) && rm $(output_filename)
cvrR:
	@clear && $(cc) $(flags) $(files) $(output_filename) $(post_flags) && valgrind $(valgrind_flags) ./$(output_filename) $(arguments) && rm $(output_filename)
cvlrR:
	@clear && $(cc) $(flags) $(files) $(output_filename) $(post_flags) && valgrind $(valgrind_flags) $(valgrind_log) ./$(output_filename) $(arguments) && rm $(output_filename)
c:
	@clear && $(cc) $(flags) $(files) $(output_filename) $(post_flags)
r:
	@clear && ./$(output_filename) $(arguments)
vr:
	@clear && valgrind ./$(output_filename) $(arguments)
R:
	@clear && rm $(output_filename)
