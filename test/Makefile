all: test_tsv test_wzio

test_tsv:
	gcc -g test_tsv.c -I.. -lz -o test_tsv_bin
	./test_tsv_bin test.tsv

test_wzio:
	gcc -g -Wall test_wzio.c -I.. -lz -o test_wzio_bin
	./test_wzio_bin

clean:
	rm -f test_tsv_bin test_wzio_bin
